// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "platform/process_probe.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <utility>

struct RecordedProbeCall {
  QString executable;
  QStringList arguments;
};

class RecordingProcessProbe final : public ProcessProbe {
public:
  void expect(QString executable, QStringList arguments, ProbeResult result) {
    expectations_.append({
        .call =
            {
                .executable = std::move(executable),
                .arguments = std::move(arguments),
            },
        .result = std::move(result),
    });
  }

  ProbeResult run(const QString &executable, const QStringList &arguments) override {
    calls.append({.executable = executable, .arguments = arguments});
    if (nextExpectation_ >= expectations_.size()) {
      unexpectedCall_ = true;
      return {.status = ProbeResult::Status::Failed, .standardOutput = {}};
    }

    const Expectation &expectation = expectations_.at(nextExpectation_++);
    exactCalls_ = exactCalls_ && expectation.call.executable == executable && expectation.call.arguments == arguments;
    return expectation.result;
  }

  [[nodiscard]] bool expectationsMet() const {
    return exactCalls_ && !unexpectedCall_ && nextExpectation_ == expectations_.size();
  }

  QList<RecordedProbeCall> calls;

private:
  struct Expectation {
    RecordedProbeCall call;
    ProbeResult result;
  };

  QList<Expectation> expectations_;
  qsizetype nextExpectation_ = 0;
  bool exactCalls_ = true;
  bool unexpectedCall_ = false;
};
