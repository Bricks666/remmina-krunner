// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "dbus/runner_service.h"

#include "core/instance_registry.h"
#include "core/matcher.h"
#include "core/profile_catalog.h"
#include "core/remmina_launcher.h"

#include <QStringList>
#include <QTimer>

#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace RemminaKRunner {
namespace {

// These are the stable values in KF6 KRunner::QueryMatch::CategoryRelevance;
// they also retain the traditional informational/possible/exact wire ranking.
constexpr int informationalMatch = 30;
constexpr int possibleMatch = 50;
constexpr int exactMatch = 100;

constexpr QLatin1StringView iconName{"org.remmina.Remmina"};
constexpr QLatin1StringView newActionId{"action:new"};
constexpr QLatin1StringView actionPrefix{"action:"};
constexpr QLatin1StringView errorPrefix{"error:"};

int timerInterval(std::chrono::milliseconds timeout) noexcept
{
    if (timeout.count() <= 0) {
        return 0;
    }
    if (timeout.count() > std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(timeout.count());
}

QVariantMap visibleProperties(QString subtext)
{
    return {
        {QStringLiteral("subtext"), std::move(subtext)},
        {QStringLiteral("category"), QStringLiteral("Remmina")},
        {QStringLiteral("actions"), QStringList{}},
    };
}

RemoteMatch errorMatch(QString id, QString subtext)
{
    return {
        std::move(id),
        QStringLiteral("Remmina unavailable"),
        QString{iconName},
        informationalMatch,
        0.0,
        visibleProperties(std::move(subtext)),
    };
}

RemoteMatches creationResult()
{
    return {{
        QString{newActionId},
        QStringLiteral("Create a new Remmina connection"),
        QString{iconName},
        exactMatch,
        1.0,
        visibleProperties(QStringLiteral("Create a new connection profile")),
    }};
}

RemoteMatches noInstanceError()
{
    return {errorMatch(QStringLiteral("error:no-instance"),
                       QStringLiteral("No Remmina installation is available"))};
}

RemoteMatches noProfilesError()
{
    return {errorMatch(QStringLiteral("error:no-profiles"),
                       QStringLiteral("No Remmina profiles are available"))};
}

RemoteMatches unreadableError()
{
    return {errorMatch(QStringLiteral("error:unreadable"),
                       QStringLiteral("Remmina profiles cannot be read"))};
}

RemoteMatches internalError()
{
    return {errorMatch(QStringLiteral("error:internal"),
                       QStringLiteral("Unable to search Remmina profiles"))};
}

QVariantMap runnerConfig()
{
    return {
        {QStringLiteral("MatchRegex"), QStringLiteral("(?i)^rem(?:\\s.*)?$")},
    };
}

std::optional<RemminaInstance> selectedInstance(const RegistrySnapshot &snapshot)
{
    if (snapshot.selectedId.isEmpty()) {
        return std::nullopt;
    }
    const RemminaInstance *selected = nullptr;
    for (const RemminaInstance &instance : snapshot.instances) {
        if (instance.id != snapshot.selectedId) {
            continue;
        }
        if (selected != nullptr) {
            return std::nullopt;
        }
        selected = &instance;
    }
    return selected == nullptr ? std::nullopt
                               : std::optional<RemminaInstance>{*selected};
}

QString profileSubtext(const ProfileRecord &record)
{
    QStringList components;
    for (const QString &value : {record.protocol, record.server, record.labelsDisplay}) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty()) {
            components.append(trimmed);
        }
    }
    return components.join(QStringLiteral(" · "));
}

} // namespace

RunnerService::RunnerService(InstanceRegistryControlSource &registry,
                             ProfileCatalogAccess &catalog,
                             RemminaLaunchSource &launcher,
                             QObject *parent)
    : RunnerService(registry, catalog, launcher, std::chrono::seconds(30), parent)
{
}

RunnerService::RunnerService(InstanceRegistryControlSource &registry,
                             ProfileCatalogAccess &catalog,
                             RemminaLaunchSource &launcher,
                             std::chrono::milliseconds catalogIdleTimeout,
                             QObject *parent)
    : QObject(parent)
    , registry_(registry)
    , catalog_(catalog)
    , launcher_(launcher)
    , catalogIdleTimer_(new QTimer(this))
    , creationResult_(creationResult())
    , noInstanceError_(noInstanceError())
    , noProfilesError_(noProfilesError())
    , unreadableError_(unreadableError())
    , internalError_(internalError())
    , config_(runnerConfig())
{
    static_assert(std::is_nothrow_copy_constructible_v<RemoteMatches>);
    static_assert(std::is_nothrow_copy_constructible_v<QVariantMap>);
    registerDbusTypes();
    catalogIdleTimer_->setSingleShot(true);
    catalogIdleTimer_->setInterval(timerInterval(catalogIdleTimeout));
    connect(catalogIdleTimer_, &QTimer::timeout, this, [this] {
        endActiveSession(true);
    });
}

RunnerService::~RunnerService() noexcept
{
    endActiveSession(true);
}

RemoteMatches RunnerService::Match(const QString &query) noexcept
{
    activationToken_.clear();
    try {
        const ParsedQuery parsed = parseRunnerQuery(query);
        if (parsed.kind == QueryKind::Ignore) {
            offeredProfileIds_.clear();
            return {};
        }
        if (parsed.kind == QueryKind::Create) {
            offeredProfileIds_.clear();
            return creationResult_;
        }

        const RegistrySnapshot snapshot = registry_.snapshot();
        const std::optional<RemminaInstance> instance = selectedInstance(snapshot);
        if (!instance.has_value()) {
            endActiveSession(false);
            return noInstanceError_;
        }

        sessionActive_ = true;
        const CatalogResult result = catalog_.records(*instance);
        if (const auto *error = std::get_if<ProfileCatalogError>(&result)) {
            endActiveSession(false);
            return *error == ProfileCatalogError::NoProfileDirectory
                ? noProfilesError_
                : unreadableError_;
        }

        const QList<SearchMatch> matches =
            matchProfiles(std::get<QList<ProfileRecord>>(result), parsed.tokens);
        RemoteMatches remoteMatches;
        QSet<QString> offeredProfileIds;
        remoteMatches.reserve(matches.size());
        offeredProfileIds.reserve(matches.size());
        for (const SearchMatch &match : matches) {
            if (match.record.opaqueId.isEmpty()) {
                continue;
            }
            remoteMatches.append({
                match.record.opaqueId,
                match.record.name,
                QString{iconName},
                possibleMatch,
                match.relevance,
                visibleProperties(profileSubtext(match.record)),
            });
            offeredProfileIds.insert(match.record.opaqueId);
        }
        offeredProfileIds_.swap(offeredProfileIds);
        catalogIdleTimer_->start();
        return remoteMatches;
    } catch (...) {
        endActiveSession(false);
        return internalError_;
    }
}

RunnerActions RunnerService::Actions() noexcept
{
    return {};
}

void RunnerService::Run(const QString &matchId, const QString &actionId) noexcept
{
    QString activationToken;
    activationToken.swap(activationToken_);
    try {
        if (actionId.isEmpty() && !matchId.isEmpty()
            && !matchId.startsWith(errorPrefix)) {
            if (matchId == newActionId) {
                launcher_.create(QStringView{activationToken});
            } else if (!matchId.startsWith(actionPrefix)
                       && offeredProfileIds_.contains(matchId)) {
                launcher_.connect(QStringView{matchId}, QStringView{activationToken});
            }
        }
    } catch (...) {
    }
    endActiveSession(false);
}

void RunnerService::Teardown() noexcept
{
    teardownEveryCall();
}

QVariantMap RunnerService::Config() noexcept
{
    endActiveSession(true);
    try {
        registry_.rescanAndRepair();
    } catch (...) {
    }
    try {
        catalog_.reset();
    } catch (...) {
    }
    return config_;
}

void RunnerService::SetActivationToken(const QString &token) noexcept
{
    try {
        activationToken_ = token;
    } catch (...) {
        activationToken_.clear();
    }
}

void RunnerService::endActiveSession(bool clearActivationToken) noexcept
{
    catalogIdleTimer_->stop();
    offeredProfileIds_.clear();
    if (clearActivationToken) {
        activationToken_.clear();
    }
    if (!sessionActive_) {
        return;
    }
    sessionActive_ = false;
    try {
        catalog_.endSession();
    } catch (...) {
    }
}

void RunnerService::teardownEveryCall() noexcept
{
    catalogIdleTimer_->stop();
    activationToken_.clear();
    offeredProfileIds_.clear();
    sessionActive_ = false;
    try {
        catalog_.endSession();
    } catch (...) {
    }
}

} // namespace RemminaKRunner
