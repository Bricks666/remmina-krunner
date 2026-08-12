#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

if [[ $# -lt 4 || $# -gt 5 ]]; then
    echo "Usage: $0 OWNER/REPOSITORY TAG ASSET_DIRECTORY EXPECTED_COMMIT [NONCE]" >&2
    exit 64
fi

repository=$1
release_tag=$2
asset_directory=$3
expected_commit=$4
publication_nonce=${5:-${GITHUB_RUN_ID:-manual}}
temporary_root=
release_id=
archive_pending_id=
checksum_pending_id=
archive_pending_name=
checksum_pending_name=
prior_archive_id=
prior_checksum_id=
recovery_archive_id=
recovery_checksum_id=
lookup_release_id=
lookup_release_draft=

die()
{
    local message=$1
    local status=${2:-1}
    echo "${message}" >&2
    exit "${status}"
}

require_commands()
{
    local command_name
    for command_name in cmp find gh mktemp readlink rm sha256sum sort stat; do
        command -v "${command_name}" >/dev/null 2>&1 ||
            die "Required release publication command is unavailable: ${command_name}" 69
    done
}

release_record_by_id()
{
    gh api \
        --jq '[.id, .draft, .tag_name] | @tsv' \
        "repos/${repository}/releases/${release_id}"
}

valid_object_id()
{
    [[ $1 =~ ^([0-9a-f]{40}|[0-9a-f]{64})$ ]]
}

resolve_tag_commit()
{
    local object_record object_type object_id depth
    object_record=$(gh api \
        --jq '[.object.type, .object.sha] | @tsv' \
        "repos/${repository}/git/ref/tags/${release_tag}") || return 1
    for ((depth = 0; depth < 8; depth += 1)); do
        IFS=$'\t' read -r object_type object_id <<<"${object_record}"
        valid_object_id "${object_id}" || return 1
        case ${object_type} in
            commit)
                printf '%s\n' "${object_id}"
                return
                ;;
            tag)
                object_record=$(gh api \
                    --jq '[.object.type, .object.sha] | @tsv' \
                    "repos/${repository}/git/tags/${object_id}") || return 1
                ;;
            *)
                return 1
                ;;
        esac
    done
    return 1
}

tag_matches_expected_commit()
{
    local observed_commit
    observed_commit=$(resolve_tag_commit) || return 1
    [[ ${observed_commit} == "${expected_commit}" ]]
}

lookup_release_by_tag()
{
    local records_path=${temporary_root}/release-records.tsv
    local record_id record_draft record_tag extra_field
    local record_count=0 match_count=0

    lookup_release_id=
    lookup_release_draft=
    if ! gh api \
        --paginate \
        --jq '.[] | [.id, .draft, .tag_name] | @tsv' \
        "repos/${repository}/releases?per_page=100" >"${records_path}"; then
        return 2
    fi

    while IFS=$'\t' read -r record_id record_draft record_tag extra_field; do
        ((record_count += 1))
        if ((record_count > 10000)) ||
            [[ ! ${record_id} =~ ^[1-9][0-9]*$ ||
               ! ${record_draft} =~ ^(true|false)$ ||
               -z ${record_tag} || -n ${extra_field} ]]; then
            return 3
        fi
        [[ ${record_tag} == "${release_tag}" ]] || continue
        ((match_count += 1))
        ((match_count == 1)) || return 4
        lookup_release_id=${record_id}
        lookup_release_draft=${record_draft}
    done <"${records_path}"

    ((match_count == 1)) || return 1
}

ensure_captured_release_is_draft()
{
    local record observed_id observed_draft observed_tag
    record=$(release_record_by_id) || return 1
    IFS=$'\t' read -r observed_id observed_draft observed_tag <<<"${record}"
    [[ ${observed_id} == "${release_id}" &&
       ${observed_draft} == true &&
       ${observed_tag} == "${release_tag}" ]]
}

ensure_publication_identity()
{
    ensure_captured_release_is_draft && tag_matches_expected_commit
}

list_assets()
{
    gh api \
        --paginate \
        --jq '.[] | [.id, .name] | @tsv' \
        "repos/${repository}/releases/${release_id}/assets?per_page=100"
}

lookup_asset_id_by_name()
{
    local wanted_name=$1
    local assets asset_id asset_name found_id=
    lookup_asset_id=
    assets=$(list_assets) || return 2
    while IFS=$'\t' read -r asset_id asset_name; do
        [[ -n ${asset_id} ]] || continue
        [[ ${asset_id} =~ ^[0-9]+$ ]] || return 2
        if [[ ${asset_name} == "${wanted_name}" ]]; then
            [[ -z ${found_id} ]] || return 3
            found_id=${asset_id}
        fi
    done <<<"${assets}"
    [[ -n ${found_id} ]] || return 1
    lookup_asset_id=${found_id}
}

lookup_asset_name_by_id()
{
    local wanted_id=$1
    local assets asset_id asset_name found_name=
    lookup_asset_name=
    assets=$(list_assets) || return 2
    while IFS=$'\t' read -r asset_id asset_name; do
        [[ -z ${asset_id} || ${asset_id} =~ ^[0-9]+$ ]] || return 2
        if [[ ${asset_id} == "${wanted_id}" ]]; then
            [[ -z ${found_name} ]] || return 3
            found_name=${asset_name}
        fi
    done <<<"${assets}"
    [[ -n ${found_name} ]] || return 1
    lookup_asset_name=${found_name}
}

delete_pending_during_cleanup()
{
    local pending_id=$1
    local pending_name=$2
    local lookup_result
    [[ -n ${release_id} && -n ${pending_id} && -n ${pending_name} ]] || return
    ensure_publication_identity >/dev/null 2>&1 || return
    lookup_result=0
    lookup_asset_name_by_id "${pending_id}" >/dev/null 2>&1 || lookup_result=$?
    [[ ${lookup_result} -eq 0 && ${lookup_asset_name} == "${pending_name}" ]] || return
    gh api --method DELETE \
        "repos/${repository}/releases/assets/${pending_id}" >/dev/null 2>&1 || return
    ensure_publication_identity >/dev/null 2>&1 || true
}

cleanup()
{
    local cleanup_status=$?
    trap - EXIT
    set +e
    if [[ ${cleanup_status} -ne 0 ]]; then
        delete_pending_during_cleanup "${archive_pending_id}" "${archive_pending_name}"
        delete_pending_during_cleanup "${checksum_pending_id}" "${checksum_pending_name}"
    fi
    if [[ -n ${temporary_root} &&
          ${temporary_root} == /tmp/remmina-publish-release.* &&
          ${temporary_root} != /tmp ]]; then
        rm -rf -- "${temporary_root}" >/dev/null 2>&1 || true
    fi
    exit "${cleanup_status}"
}
trap cleanup EXIT

if [[ ! ${repository} =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ||
      ${repository} == *..* ]]; then
    die "Repository must use canonical OWNER/REPOSITORY form" 64
fi
if [[ ! ${release_tag} =~ ^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
    die "Release tag must use canonical vMAJOR.MINOR.PATCH form" 64
fi
if ! valid_object_id "${expected_commit}"; then
    die "Expected commit must be a canonical GitHub object ID" 64
fi
if [[ ! ${publication_nonce} =~ ^[A-Za-z0-9][A-Za-z0-9.-]{0,63}$ ]]; then
    die "Publication nonce contains unsafe characters" 64
fi
if [[ -z ${GH_TOKEN:-} ]]; then
    die "GH_TOKEN is required for release publication" 64
fi
if [[ ${asset_directory} != /* || ${asset_directory} == / ||
      ${asset_directory} == *$'\n'* || ${asset_directory} == *$'\r'* ||
      ! -d ${asset_directory} || -L ${asset_directory} ]]; then
    die "Asset directory must be a bounded real absolute directory" 64
fi

require_commands
asset_directory=$(readlink -f -- "${asset_directory}") ||
    die "Unable to canonicalize asset directory" 66
[[ ${asset_directory} != / ]] || die "Asset directory resolves to the filesystem root" 64

temporary_root=$(mktemp -d -- /tmp/remmina-publish-release.XXXXXX)
[[ ${temporary_root} == /tmp/remmina-publish-release.* &&
   ${temporary_root} != /tmp && -d ${temporary_root} && ! -L ${temporary_root} ]] ||
    die "Unable to create bounded publication staging directory" 73

archive_name=remmina-krunner-${release_tag}-linux-x86_64.tar.gz
checksum_name=${archive_name}.sha256
archive_path=${asset_directory}/${archive_name}
checksum_path=${asset_directory}/${checksum_name}

inventory_unsorted=${temporary_root}/inventory.unsorted
inventory_sorted=${temporary_root}/inventory.sorted
if ! find "${asset_directory}" -mindepth 1 -maxdepth 1 -printf '%f\0' \
    >"${inventory_unsorted}"; then
    die "Unable to enumerate local release assets" 69
fi
if ! LC_ALL=C sort -z -- "${inventory_unsorted}" >"${inventory_sorted}"; then
    die "Unable to sort local release asset inventory" 69
fi
local_inventory=()
mapfile -d '' -t local_inventory <"${inventory_sorted}"
if [[ ${#local_inventory[@]} -ne 2 ||
      ${local_inventory[0]} != "${archive_name}" ||
      ${local_inventory[1]} != "${checksum_name}" ||
      ! -f ${archive_path} || -L ${archive_path} ||
      ! -f ${checksum_path} || -L ${checksum_path} ]]; then
    die "Asset directory must contain exactly the regular archive and checksum" 66
fi
if [[ $(stat -c '%h' -- "${archive_path}") != 1 ||
      $(stat -c '%h' -- "${checksum_path}") != 1 ]]; then
    die "Release assets must not be hard-linked" 66
fi

checksum_line=$(<"${checksum_path}")
if [[ ! ${checksum_line} =~ ^([0-9a-f]{64})[[:space:]][[:space:]]${archive_name}$ ]]; then
    die "Checksum must contain the archive digest and basename" 65
fi
archive_digest=${BASH_REMATCH[1]}
(
    cd -- "${asset_directory}"
    sha256sum --check "${checksum_name}" >/dev/null
) || die "Local release checksum verification failed" 65

tag_matches_expected_commit ||
    die "Release tag does not resolve to the expected checkout commit" 65

verify_asset_bytes()
{
    local asset_id=$1
    local expected_path=$2
    local description=$3
    local downloaded_path=${temporary_root}/download-${asset_id}
    gh api \
        --header 'Accept: application/octet-stream' \
        "repos/${repository}/releases/assets/${asset_id}" >"${downloaded_path}" ||
        die "Unable to download remote ${description}" 69
    cmp -- "${downloaded_path}" "${expected_path}" ||
        die "Remote ${description} bytes do not match the verified local asset" 65
    if [[ ${expected_path} == "${archive_path}" ]]; then
        local downloaded_digest
        downloaded_digest=$(sha256sum "${downloaded_path}")
        downloaded_digest=${downloaded_digest%% *}
        [[ ${downloaded_digest} == "${archive_digest}" ]] ||
            die "Remote archive digest does not match its checksum" 65
    fi
}

verify_final_pair()
{
    local assets asset_id asset_name
    local final_archive_id= final_checksum_id= count=0
    ensure_publication_identity ||
        die "Release identity, draft state, or tag target changed" 73
    assets=$(list_assets) || die "Unable to list release assets" 69
    while IFS=$'\t' read -r asset_id asset_name; do
        [[ -n ${asset_id} ]] || continue
        ((count += 1))
        [[ ${asset_id} =~ ^[0-9]+$ ]] || die "Release asset ID is invalid" 65
        case ${asset_name} in
            "${archive_name}")
                [[ -z ${final_archive_id} ]] || die "Duplicate release archive asset" 65
                final_archive_id=${asset_id}
                ;;
            "${checksum_name}")
                [[ -z ${final_checksum_id} ]] || die "Duplicate release checksum asset" 65
                final_checksum_id=${asset_id}
                ;;
            *)
                die "Final release contains an unexpected asset: ${asset_name}" 65
                ;;
        esac
    done <<<"${assets}"
    [[ ${count} -eq 2 && -n ${final_archive_id} && -n ${final_checksum_id} ]] ||
        die "Final release must contain exactly the archive and checksum" 65
    verify_asset_bytes "${final_archive_id}" "${archive_path}" archive
    verify_asset_bytes "${final_checksum_id}" "${checksum_path}" checksum
    ensure_publication_identity ||
        die "Release identity or tag target changed during final verification" 73
}

require_asset_name_absent()
{
    local asset_name=$1
    local lookup_result=0
    lookup_asset_id_by_name "${asset_name}" || lookup_result=$?
    case ${lookup_result} in
        0)
            die "Release already contains pending asset ${asset_name}" 73
            ;;
        1)
            return
            ;;
        2)
            die "Unable to list assets while checking ${asset_name}" 69
            ;;
        3)
            die "Release contains ambiguous assets named ${asset_name}" 65
            ;;
        *)
            die "Unexpected asset lookup result" 70
            ;;
    esac
}

upload_pending_asset()
{
    local local_path=$1
    local pending_name=$2
    local result_variable=$3
    local description=$4
    local upload_record uploaded_id uploaded_name lookup_result=0

    ensure_publication_identity ||
        die "Release identity changed before ${description} upload" 73
    upload_record=$(gh api \
        --method POST \
        --header 'Content-Type: application/octet-stream' \
        --input "${local_path}" \
        --jq '[.id, .name] | @tsv' \
        "https://uploads.github.com/repos/${repository}/releases/${release_id}/assets?name=${pending_name}") ||
        die "Unable to upload ${description}" 69
    IFS=$'\t' read -r uploaded_id uploaded_name <<<"${upload_record}"
    [[ ${uploaded_id} =~ ^[0-9]+$ && ${uploaded_name} == "${pending_name}" ]] ||
        die "${description} upload returned an invalid asset identity" 65
    printf -v "${result_variable}" '%s' "${uploaded_id}"

    ensure_publication_identity ||
        die "Release identity changed after ${description} upload" 73
    lookup_asset_name_by_id "${uploaded_id}" || lookup_result=$?
    [[ ${lookup_result} -eq 0 && ${lookup_asset_name} == "${pending_name}" ]] ||
        die "${description} response ID is absent from the captured release" 65
}

release_lookup_result=0
lookup_release_by_tag || release_lookup_result=$?
if [[ ${release_lookup_result} -eq 1 ]]; then
    tag_matches_expected_commit ||
        die "Release tag moved before draft creation" 73
    gh release create "${release_tag}" \
        "${archive_path}" \
        "${checksum_path}" \
        --repo "${repository}" \
        --draft \
        --generate-notes \
        --verify-tag ||
        die "Unable to create draft release" 69

    release_lookup_result=0
    lookup_release_by_tag || release_lookup_result=$?
    [[ ${release_lookup_result} -eq 0 ]] ||
        die "Unable to read newly created draft release" 69
    release_id=${lookup_release_id}
    release_draft=${lookup_release_draft}
    [[ ${release_draft} == true ]] ||
        die "New release is not the expected draft" 73
    ensure_publication_identity ||
        die "New draft or release tag changed after creation" 73
    verify_final_pair
    exit 0
fi

case ${release_lookup_result} in
    0)
        ;;
    2)
        die "Unable to list repository releases" 69
        ;;
    3)
        die "Release collection returned an invalid record" 65
        ;;
    4)
        die "Release collection contains duplicate exact-tag records" 65
        ;;
    *)
        die "Unexpected release lookup result" 70
        ;;
esac
release_id=${lookup_release_id}
release_draft=${lookup_release_draft}
[[ ${release_draft} == true ]] ||
    die "Refusing to mutate published release ${release_tag}" 73
ensure_publication_identity ||
    die "Release identity, draft state, or tag target changed" 73

existing_assets=$(list_assets) || die "Unable to list existing draft assets" 69
archive_pending_name=${archive_name}.pending-${publication_nonce}
checksum_pending_name=${checksum_name}.pending-${publication_nonce}
while IFS=$'\t' read -r asset_id asset_name; do
    [[ -n ${asset_id} ]] || continue
    [[ ${asset_id} =~ ^[0-9]+$ ]] || die "Release asset ID is invalid" 65
    case ${asset_name} in
        "${archive_name}")
            [[ -z ${prior_archive_id} ]] || die "Draft contains duplicate archive assets" 65
            prior_archive_id=${asset_id}
            ;;
        "${checksum_name}")
            [[ -z ${prior_checksum_id} ]] || die "Draft contains duplicate checksum assets" 65
            prior_checksum_id=${asset_id}
            ;;
        "${archive_pending_name}")
            [[ -z ${recovery_archive_id} ]] || die "Draft contains duplicate pending archive assets" 65
            recovery_archive_id=${asset_id}
            ;;
        "${checksum_pending_name}")
            [[ -z ${recovery_checksum_id} ]] || die "Draft contains duplicate pending checksum assets" 65
            recovery_checksum_id=${asset_id}
            ;;
        *)
            die "Refusing to mutate a draft containing foreign asset: ${asset_name}" 65
            ;;
    esac
done <<<"${existing_assets}"

# An upload API failure can write the asset while withholding its response ID.
# Verify every recoverable pending asset before mutating any of them.
for recovery_record in \
    "${recovery_archive_id}|${archive_pending_name}|${archive_path}|archive" \
    "${recovery_checksum_id}|${checksum_pending_name}|${checksum_path}|checksum"; do
    recovery_id=${recovery_record%%|*}
    [[ -n ${recovery_id} ]] || continue
    recovery_tail=${recovery_record#*|}
    recovery_name=${recovery_tail%%|*}
    recovery_tail=${recovery_tail#*|}
    recovery_path=${recovery_tail%%|*}
    recovery_description=${recovery_tail#*|}
    ensure_publication_identity ||
        die "Release identity changed before pending asset verification" 73
    verify_asset_bytes "${recovery_id}" "${recovery_path}" \
        "recoverable pending ${recovery_description}"
    ensure_publication_identity ||
        die "Release identity changed during pending asset verification" 73
done

for recovery_record in \
    "${recovery_archive_id}|${archive_pending_name}" \
    "${recovery_checksum_id}|${checksum_pending_name}"; do
    recovery_id=${recovery_record%%|*}
    [[ -n ${recovery_id} ]] || continue
    recovery_name=${recovery_record#*|}
    ensure_publication_identity ||
        die "Release identity changed before pending asset cleanup" 73
    recovery_lookup=0
    lookup_asset_id_by_name "${recovery_name}" || recovery_lookup=$?
    [[ ${recovery_lookup} -eq 0 && ${lookup_asset_id} == "${recovery_id}" ]] ||
        die "Pending asset identity or uniqueness changed before cleanup" 73
    gh api --method DELETE \
        "repos/${repository}/releases/assets/${recovery_id}" >/dev/null ||
        die "Unable to remove verified interrupted upload" 69
    ensure_publication_identity ||
        die "Release identity changed after pending asset cleanup" 73
done

require_asset_name_absent "${archive_pending_name}"
require_asset_name_absent "${checksum_pending_name}"

upload_pending_asset \
    "${archive_path}" \
    "${archive_pending_name}" \
    archive_pending_id \
    "staged archive"
verify_asset_bytes "${archive_pending_id}" "${archive_path}" "staged archive"

upload_pending_asset \
    "${checksum_path}" \
    "${checksum_pending_name}" \
    checksum_pending_id \
    "staged checksum"
verify_asset_bytes "${checksum_pending_id}" "${checksum_path}" "staged checksum"

# GitHub does not provide an atomic two-asset swap. Both pending assets are
# uploaded and byte-verified before any prior asset is deleted. From here a
# failed API call can leave a repairable draft, but never intentionally mutates
# a release observed as published.
for asset_record in "${prior_archive_id}|${archive_name}" "${prior_checksum_id}|${checksum_name}"; do
    asset_id=${asset_record%%|*}
    asset_name=${asset_record#*|}
    [[ -n ${asset_id} ]] || continue
    ensure_publication_identity ||
        die "Release identity changed before deleting asset ${asset_id}" 73
    lookup_asset_name_by_id "${asset_id}" ||
        die "Prior draft asset identity changed before replacement" 73
    [[ ${lookup_asset_name} == "${asset_name}" ]] ||
        die "Prior draft asset name changed before replacement" 73
    gh api --method DELETE \
        "repos/${repository}/releases/assets/${asset_id}" >/dev/null ||
        die "Unable to remove prior draft asset ${asset_id}" 69
    ensure_publication_identity ||
        die "Release identity changed after deleting asset ${asset_id}" 73
done

ensure_publication_identity ||
    die "Release identity changed before archive rename" 73
renamed_archive=$(gh api \
    --method PATCH \
    "repos/${repository}/releases/assets/${archive_pending_id}" \
    -f "name=${archive_name}" \
    --jq '.name') || die "Unable to rename staged archive" 69
[[ ${renamed_archive} == "${archive_name}" ]] || die "Staged archive rename was not confirmed" 65
archive_pending_id=
ensure_publication_identity ||
    die "Release identity changed after archive rename" 73

ensure_publication_identity ||
    die "Release identity changed before checksum rename" 73
renamed_checksum=$(gh api \
    --method PATCH \
    "repos/${repository}/releases/assets/${checksum_pending_id}" \
    -f "name=${checksum_name}" \
    --jq '.name') || die "Unable to rename staged checksum" 69
[[ ${renamed_checksum} == "${checksum_name}" ]] || die "Staged checksum rename was not confirmed" 65
checksum_pending_id=
ensure_publication_identity ||
    die "Release identity changed after checksum rename" 73

verify_final_pair
