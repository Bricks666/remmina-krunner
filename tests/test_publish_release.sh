#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

state_add_asset()
{
    local name=$1
    local source=$2
    local asset_id
    asset_id=$(<"${GH_STATE_DIR}/next-id")
    printf '%s\n' "$((asset_id + 1))" >"${GH_STATE_DIR}/next-id"
    cp -- "${source}" "${GH_STATE_DIR}/assets/${asset_id}"
    printf '%s\n' "${name}" >"${GH_STATE_DIR}/names/${asset_id}"
    printf '%s\n' "${asset_id}"
}

fake_gh()
{
    printf '%q ' "$@" >>"${GH_STATE_DIR}/calls"
    printf '\n' >>"${GH_STATE_DIR}/calls"
    local command_name=${1:-}
    shift || true

    if [[ ${command_name} == api ]]; then
        local include=0 paginate=0 method=GET jq_filter= endpoint= field=
        local hostname=github.com input_file= content_type=
        while [[ $# -gt 0 ]]; do
            case $1 in
                --silent)
                    shift
                    ;;
                --include)
                    include=1
                    shift
                    ;;
                --paginate)
                    paginate=1
                    shift
                    ;;
                --method)
                    method=$2
                    shift 2
                    ;;
                --jq)
                    jq_filter=$2
                    shift 2
                    ;;
                -H|--header)
                    if [[ $2 == 'Content-Type: application/octet-stream' ]]; then
                        content_type=$2
                    fi
                    shift 2
                    ;;
                --hostname)
                    hostname=$2
                    shift 2
                    ;;
                --input)
                    input_file=$2
                    shift 2
                    ;;
                -f)
                    field=$2
                    shift 2
                    ;;
                *)
                    endpoint=$1
                    shift
                    ;;
            esac
        done

        local release_state release_id release_tag
        release_state=$(<"${GH_STATE_DIR}/release-state")
        release_id=$(<"${GH_STATE_DIR}/release-id")
        release_tag=$(<"${GH_STATE_DIR}/release-tag")

        if [[ ${endpoint} == "repos/${EXPECTED_REPO}/git/ref/tags/${release_tag}" ]]; then
            local tag_check_count=0 tag_kind tag_commit tag_object
            [[ ! -f ${GH_STATE_DIR}/tag-check-count ]] ||
                tag_check_count=$(<"${GH_STATE_DIR}/tag-check-count")
            tag_check_count=$((tag_check_count + 1))
            printf '%s\n' "${tag_check_count}" >"${GH_STATE_DIR}/tag-check-count"
            tag_kind=$(<"${GH_STATE_DIR}/tag-kind")
            tag_commit=$(<"${GH_STATE_DIR}/tag-commit")
            tag_object=$(<"${GH_STATE_DIR}/tag-object")
            if [[ ${tag_kind} == annotated ]]; then
                printf 'tag\t%s\n' "${tag_object}"
            else
                printf 'commit\t%s\n' "${tag_commit}"
            fi
            return
        fi

        if [[ ${endpoint} == "repos/${EXPECTED_REPO}/git/tags/$(<"${GH_STATE_DIR}/tag-object")" ]]; then
            printf 'commit\t%s\n' "$(<"${GH_STATE_DIR}/tag-commit")"
            return
        fi

        if [[ ${endpoint} == "repos/${EXPECTED_REPO}/releases/tags/${release_tag}" ]]; then
            if [[ ${include} -eq 1 ]]; then
                local query_status
                if [[ -f ${GH_STATE_DIR}/query-status ]]; then
                    query_status=$(<"${GH_STATE_DIR}/query-status")
                elif [[ ${release_state} == published ]]; then
                    query_status=200
                else
                    query_status=404
                fi
                printf 'HTTP/2.0 %s mock\n\n' "${query_status}"
                [[ ${query_status} == 200 ]]
                return
            fi
            [[ ${release_state} == published ]] || return 1
            if [[ ${jq_filter} == *'.id'* ]]; then
                if [[ ${release_state} == draft ]]; then
                    printf '%s\ttrue\t%s\n' "${release_id}" "${release_tag}"
                else
                    printf '%s\tfalse\t%s\n' "${release_id}" "${release_tag}"
                fi
                return
            fi
        fi

        if [[ ${endpoint} == "repos/${EXPECTED_REPO}/releases?per_page=100" ]]; then
            [[ ${paginate} -eq 1 && ${jq_filter} == *'.id'* &&
               ${jq_filter} == *'.draft'* && ${jq_filter} == *'.tag_name'* ]]
            if [[ -f ${GH_STATE_DIR}/fail-next-release-list ]]; then
                rm -f -- "${GH_STATE_DIR}/fail-next-release-list"
                return 1
            fi
            if [[ -f ${GH_STATE_DIR}/release-list-override ]]; then
                cat -- "${GH_STATE_DIR}/release-list-override"
                return
            fi
            if [[ -f ${GH_STATE_DIR}/release-list-prefix ]]; then
                cat -- "${GH_STATE_DIR}/release-list-prefix"
            fi
            if [[ ${release_state} != absent ]]; then
                if [[ ${release_state} == draft ]]; then
                    printf '%s\ttrue\t%s\n' "${release_id}" "${release_tag}"
                else
                    printf '%s\tfalse\t%s\n' "${release_id}" "${release_tag}"
                fi
            fi
            if [[ -f ${GH_STATE_DIR}/release-list-extra-match ]]; then
                if [[ ${release_state} == draft ]]; then
                    printf '%s\ttrue\t%s\n' "$((release_id + 1))" "${release_tag}"
                else
                    printf '%s\tfalse\t%s\n' "$((release_id + 1))" "${release_tag}"
                fi
            fi
            return
        fi

        if [[ ${endpoint} == "repos/${EXPECTED_REPO}/releases/${release_id}" ]]; then
            if [[ -f ${GH_STATE_DIR}/publish-on-next-check ]]; then
                printf '%s\n' published >"${GH_STATE_DIR}/release-state"
                rm -f -- "${GH_STATE_DIR}/publish-on-next-check"
                release_state=published
            fi
            if [[ -f ${GH_STATE_DIR}/move-tag-on-release-check ]]; then
                cp -- "${GH_STATE_DIR}/moved-commit" "${GH_STATE_DIR}/tag-commit"
                rm -f -- "${GH_STATE_DIR}/move-tag-on-release-check"
            fi
            rm -f -- "${GH_STATE_DIR}/mutation-needs-postcheck"
            : >"${GH_STATE_DIR}/draft-check-ready"
            if [[ ${release_state} == draft ]]; then
                printf '%s\ttrue\t%s\n' "${release_id}" "${release_tag}"
            else
                printf '%s\tfalse\t%s\n' "${release_id}" "${release_tag}"
            fi
            return
        fi

        if [[ ${endpoint} == "repos/${EXPECTED_REPO}/releases/${release_id}/assets?per_page=100" ]]; then
            [[ ${paginate} -eq 1 ]]
            if [[ -f ${GH_STATE_DIR}/fail-next-asset-list ]]; then
                rm -f -- "${GH_STATE_DIR}/fail-next-asset-list"
                return 1
            fi
            local name_file asset_id
            for name_file in "${GH_STATE_DIR}"/names/*; do
                [[ -e ${name_file} ]] || continue
                asset_id=${name_file##*/}
                printf '%s\t%s\n' "${asset_id}" "$(<"${name_file}")"
            done | sort -n
            return
        fi

        if [[ ${endpoint} == "https://uploads.github.com/repos/${EXPECTED_REPO}/releases/${release_id}/assets?name="* &&
              ${method} == POST ]]; then
            [[ ${hostname} == github.com ]]
            [[ ${content_type} == 'Content-Type: application/octet-stream' ]]
            [[ -f ${input_file} ]]
            [[ $(<"${GH_STATE_DIR}/release-state") == draft ]]
            [[ -f ${GH_STATE_DIR}/draft-check-ready &&
               ! -f ${GH_STATE_DIR}/mutation-needs-postcheck ]]
            rm -f -- "${GH_STATE_DIR}/draft-check-ready"
            local pending_name=${endpoint#*'?name='}
            local upload_count=0 uploaded_id
            [[ ! -f ${GH_STATE_DIR}/upload-count ]] ||
                upload_count=$(<"${GH_STATE_DIR}/upload-count")
            upload_count=$((upload_count + 1))
            printf '%s\n' "${upload_count}" >"${GH_STATE_DIR}/upload-count"
            uploaded_id=$(state_add_asset "${pending_name}" "${input_file}")
            : >"${GH_STATE_DIR}/mutation-needs-postcheck"
            if [[ -f ${GH_STATE_DIR}/fail-upload-after-write &&
                  $(<"${GH_STATE_DIR}/fail-upload-after-write") == "${upload_count}" ]]; then
                return 1
            fi
            if [[ ${upload_count} -eq 2 && -f ${GH_STATE_DIR}/move-tag-after-uploads ]]; then
                cp -- "${GH_STATE_DIR}/moved-commit" "${GH_STATE_DIR}/tag-commit"
            fi
            printf '%s\t%s\n' "${uploaded_id}" "${pending_name}"
            return
        fi

        if [[ ${endpoint} =~ ^repos/${EXPECTED_REPO}/releases/assets/([0-9]+)$ ]]; then
            local asset_id=${BASH_REMATCH[1]}
            [[ -f ${GH_STATE_DIR}/names/${asset_id} ]] || return 1
            case ${method} in
                GET)
                    local download_count=0
                    [[ ! -f ${GH_STATE_DIR}/download-count ]] ||
                        download_count=$(<"${GH_STATE_DIR}/download-count")
                    download_count=$((download_count + 1))
                    printf '%s\n' "${download_count}" >"${GH_STATE_DIR}/download-count"
                    if [[ -f ${GH_STATE_DIR}/move-tag-on-download-count &&
                          $(<"${GH_STATE_DIR}/move-tag-on-download-count") == "${download_count}" ]]; then
                        cp -- "${GH_STATE_DIR}/moved-commit" "${GH_STATE_DIR}/tag-commit"
                    fi
                    if [[ -f ${GH_STATE_DIR}/corrupt-next-download ]]; then
                        rm -f -- "${GH_STATE_DIR}/corrupt-next-download"
                        printf '%s' corrupt
                    else
                        cat -- "${GH_STATE_DIR}/assets/${asset_id}"
                    fi
                    if [[ $(<"${GH_STATE_DIR}/names/${asset_id}") == *.pending-* ]]; then
                        if [[ -f ${GH_STATE_DIR}/publish-after-pending-download ]]; then
                            : >"${GH_STATE_DIR}/publish-on-next-check"
                        fi
                        if [[ -f ${GH_STATE_DIR}/move-tag-after-pending-download ]]; then
                            : >"${GH_STATE_DIR}/move-tag-on-release-check"
                        fi
                    fi
                    ;;
                DELETE)
                    [[ $(<"${GH_STATE_DIR}/release-state") == draft ]] || return 1
                    [[ -f ${GH_STATE_DIR}/draft-check-ready &&
                       ! -f ${GH_STATE_DIR}/mutation-needs-postcheck ]] || return 1
                    rm -f -- "${GH_STATE_DIR}/draft-check-ready"
                    rm -f -- "${GH_STATE_DIR}/assets/${asset_id}" "${GH_STATE_DIR}/names/${asset_id}"
                    : >"${GH_STATE_DIR}/mutation-needs-postcheck"
                    ;;
                PATCH)
                    [[ $(<"${GH_STATE_DIR}/release-state") == draft ]] || return 1
                    [[ -f ${GH_STATE_DIR}/draft-check-ready &&
                       ! -f ${GH_STATE_DIR}/mutation-needs-postcheck ]] || return 1
                    rm -f -- "${GH_STATE_DIR}/draft-check-ready"
                    [[ ${field} == name=* ]] || return 1
                    printf '%s\n' "${field#name=}" >"${GH_STATE_DIR}/names/${asset_id}"
                    : >"${GH_STATE_DIR}/mutation-needs-postcheck"
                    local patch_count=0
                    [[ ! -f ${GH_STATE_DIR}/patch-count ]] || patch_count=$(<"${GH_STATE_DIR}/patch-count")
                    patch_count=$((patch_count + 1))
                    printf '%s\n' "${patch_count}" >"${GH_STATE_DIR}/patch-count"
                    if [[ ${patch_count} -eq 2 && -f ${GH_STATE_DIR}/inject-final-mismatch ]]; then
                        state_add_asset unexpected.txt "${GH_STATE_DIR}/inject-final-mismatch" >/dev/null
                    fi
                    if [[ ${patch_count} -eq 2 && -f ${GH_STATE_DIR}/corrupt-final-archive ]]; then
                        local candidate_id
                        for candidate_id in "${GH_STATE_DIR}"/names/*; do
                            [[ -e ${candidate_id} ]] || continue
                            if [[ $(<"${candidate_id}") == remmina-krunner-*-linux-x86_64.tar.gz ]]; then
                                printf 'corrupt final archive\n' \
                                    >"${GH_STATE_DIR}/assets/${candidate_id##*/}"
                            fi
                        done
                    fi
                    printf '%s\n' "${field#name=}"
                    ;;
                *)
                    return 1
                    ;;
            esac
            return
        fi
        return 1
    fi

    [[ ${command_name} == release ]] || return 1
    local release_command=${1:-}
    shift || true
    local tag=${1:-}
    shift || true
    local repository= paths=() argument
    local saw_draft=0 saw_generate_notes=0 saw_verify_tag=0
    while [[ $# -gt 0 ]]; do
        argument=$1
        case ${argument} in
            --repo)
                repository=$2
                shift 2
                ;;
            --draft)
                saw_draft=1
                shift
                ;;
            --generate-notes)
                saw_generate_notes=1
                shift
                ;;
            --verify-tag)
                saw_verify_tag=1
                shift
                ;;
            *)
                paths+=("${argument}")
                shift
                ;;
        esac
    done
    [[ ${repository} == "${EXPECTED_REPO}" ]] || {
        echo "gh release command omitted the explicit repository" >&2
        return 1
    }
    [[ ${tag} == "$(<"${GH_STATE_DIR}/release-tag")" ]] || return 1

    case ${release_command} in
        create)
            [[ $(<"${GH_STATE_DIR}/release-state") == absent ]]
            [[ ${#paths[@]} -eq 2 ]]
            [[ ${saw_draft} -eq 1 && ${saw_generate_notes} -eq 1 &&
               ${saw_verify_tag} -eq 1 ]]
            printf '%s\n' draft >"${GH_STATE_DIR}/release-state"
            state_add_asset "${paths[0]##*/}" "${paths[0]}" >/dev/null
            state_add_asset "${paths[1]##*/}" "${paths[1]}" >/dev/null
            if [[ -f ${GH_STATE_DIR}/move-tag-during-create ]]; then
                cp -- "${GH_STATE_DIR}/moved-commit" "${GH_STATE_DIR}/tag-commit"
            fi
            ;;
        *)
            return 1
            ;;
    esac
}

if [[ -n ${PUBLISH_RELEASE_FAIL_TOOL:-} &&
      ${0##*/} == "${PUBLISH_RELEASE_FAIL_TOOL}" ]]; then
    exit 91
fi

if [[ ${PUBLISH_RELEASE_FAKE_GH:-0} == 1 ]]; then
    fake_gh "$@"
    exit
fi

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 PUBLISH_SCRIPT" >&2
    exit 64
fi
publisher=$1
if [[ ${publisher} != /* ]]; then
    echo "Publisher path must be absolute" >&2
    exit 64
fi

test_root=$(mktemp -d /tmp/remmina-publish-release-test.XXXXXX)
cleanup()
{
    rm -rf -- "${test_root}"
}
trap cleanup EXIT

fake_bin=${test_root}/bin
mkdir -p -- "${fake_bin}"
cp -- "$0" "${fake_bin}/gh"
chmod 0755 -- "${fake_bin}/gh"

repository=test-owner/test-repo
tag=v0.1.0
expected_commit=1111111111111111111111111111111111111111
moved_commit=9999999999999999999999999999999999999999
archive=remmina-krunner-${tag}-linux-x86_64.tar.gz
checksum=${archive}.sha256
pending_archive=${archive}.pending-test-run
pending_checksum=${checksum}.pending-test-run

new_state()
{
    GH_STATE_DIR=${test_root}/state-$1
    export GH_STATE_DIR
    mkdir -p -- "${GH_STATE_DIR}/assets" "${GH_STATE_DIR}/names"
    printf '%s\n' "${2:-absent}" >"${GH_STATE_DIR}/release-state"
    printf '%s\n' 101 >"${GH_STATE_DIR}/release-id"
    printf '%s\n' "${tag}" >"${GH_STATE_DIR}/release-tag"
    printf '%s\n' lightweight >"${GH_STATE_DIR}/tag-kind"
    printf '%s\n' "${expected_commit}" >"${GH_STATE_DIR}/tag-commit"
    printf '%040d\n' 2 >"${GH_STATE_DIR}/tag-object"
    printf '%s\n' "${moved_commit}" >"${GH_STATE_DIR}/moved-commit"
    printf '%s\n' 1001 >"${GH_STATE_DIR}/next-id"
    : >"${GH_STATE_DIR}/calls"
}

new_assets()
{
    ASSET_DIRECTORY=${test_root}/assets-$1
    export ASSET_DIRECTORY
    mkdir -p -- "${ASSET_DIRECTORY}"
    printf 'new archive %s\n' "$1" >"${ASSET_DIRECTORY}/${archive}"
    (
        cd -- "${ASSET_DIRECTORY}"
        sha256sum "${archive}" >"${checksum}"
    )
}

add_old_pair()
{
    local old_archive=${GH_STATE_DIR}/old-archive
    local old_checksum=${GH_STATE_DIR}/old-checksum
    printf 'old archive\n' >"${old_archive}"
    printf 'old checksum\n' >"${old_checksum}"
    state_add_asset "${archive}" "${old_archive}" >/dev/null
    state_add_asset "${checksum}" "${old_checksum}" >/dev/null
}

asset_id_for_name()
{
    local wanted=$1 name_file
    for name_file in "${GH_STATE_DIR}"/names/*; do
        [[ -e ${name_file} ]] || continue
        if [[ $(<"${name_file}") == "${wanted}" ]]; then
            printf '%s\n' "${name_file##*/}"
            return
        fi
    done
    return 1
}

assert_exact_remote_pair()
{
    mapfile -t remote_names < <(
        for name_file in "${GH_STATE_DIR}"/names/*; do
            [[ -e ${name_file} ]] || continue
            printf '%s\n' "$(<"${name_file}")"
        done | LC_ALL=C sort
    )
    local expected=("${archive}" "${checksum}")
    if [[ ${remote_names[*]} != "${expected[*]}" ]]; then
        echo "Unexpected remote inventory: ${remote_names[*]}" >&2
        exit 1
    fi
}

assert_remote_matches_local()
{
    local name asset_id
    for name in "${archive}" "${checksum}"; do
        asset_id=$(asset_id_for_name "${name}")
        cmp -- "${GH_STATE_DIR}/assets/${asset_id}" "${ASSET_DIRECTORY}/${name}"
    done
}

assert_old_pair_preserved()
{
    assert_exact_remote_pair
    assert_old_pair_content_preserved
}

assert_old_pair_content_preserved()
{
    local archive_id checksum_id
    archive_id=$(asset_id_for_name "${archive}")
    checksum_id=$(asset_id_for_name "${checksum}")
    grep -qx 'old archive' "${GH_STATE_DIR}/assets/${archive_id}"
    grep -qx 'old checksum' "${GH_STATE_DIR}/assets/${checksum_id}"
}

pending_asset_count()
{
    local name_file count=0
    for name_file in "${GH_STATE_DIR}"/names/*; do
        [[ -e ${name_file} ]] || continue
        if [[ $(<"${name_file}") == *.pending-* ]]; then
            count=$((count + 1))
        fi
    done
    printf '%s\n' "${count}"
}

assert_no_pending_assets()
{
    local name_file
    for name_file in "${GH_STATE_DIR}"/names/*; do
        [[ -e ${name_file} ]] || continue
        if [[ $(<"${name_file}") == *.pending-* ]]; then
            echo "Pending asset was not cleaned up" >&2
            exit 1
        fi
    done
}

run_publisher()
{
    PUBLISH_RELEASE_FAKE_GH=1 \
    EXPECTED_REPO=${repository} \
    GH_TOKEN=mock-token \
    PATH="${fake_bin}:${PATH}" \
        "${publisher}" "${repository}" "${tag}" "${ASSET_DIRECTORY}" \
        "${expected_commit}" test-run
}

run_publisher_default()
{
    local attempt=$1
    PUBLISH_RELEASE_FAKE_GH=1 \
    EXPECTED_REPO=${repository} \
    GH_TOKEN=mock-token \
    GITHUB_RUN_ID=stable-run \
    GITHUB_RUN_ATTEMPT=${attempt} \
    PATH="${fake_bin}:${PATH}" \
        "${publisher}" "${repository}" "${tag}" "${ASSET_DIRECTORY}" \
        "${expected_commit}"
}

expect_failure()
{
    if run_publisher; then
        echo "Expected publisher failure for state ${GH_STATE_DIR}" >&2
        exit 1
    fi
}

expect_argument_failure()
{
    if PUBLISH_RELEASE_FAKE_GH=1 \
        EXPECTED_REPO=${repository} \
        GH_TOKEN=mock-token \
        PATH="${fake_bin}:${PATH}" \
        "${publisher}" "$@"; then
        echo "Expected publisher argument validation failure" >&2
        exit 1
    fi
    if [[ -s ${GH_STATE_DIR}/calls ]]; then
        echo "Invalid local input reached GitHub CLI" >&2
        exit 1
    fi
}

expect_tool_failure()
{
    local tool_name=$1
    cp -- "$0" "${fake_bin}/${tool_name}"
    chmod 0755 -- "${fake_bin}/${tool_name}"
    if PUBLISH_RELEASE_FAKE_GH=1 \
        PUBLISH_RELEASE_FAIL_TOOL=${tool_name} \
        EXPECTED_REPO=${repository} \
        GH_TOKEN=mock-token \
        PATH="${fake_bin}:${PATH}" \
        "${publisher}" "${repository}" "${tag}" "${ASSET_DIRECTORY}" \
        "${expected_commit}" test-run; then
        echo "Expected ${tool_name} failure to abort publication" >&2
        exit 1
    fi
    rm -f -- "${fake_bin}/${tool_name}"
    if [[ -s ${GH_STATE_DIR}/calls ]]; then
        echo "${tool_name} failure reached GitHub CLI" >&2
        exit 1
    fi
}

# Malformed identifiers and local assets fail before any GitHub request.
new_state validation absent
new_assets validation
expect_argument_failure bad-repository "${tag}" "${ASSET_DIRECTORY}" "${expected_commit}" test-run
expect_argument_failure "${repository}" v01.0.0 "${ASSET_DIRECTORY}" "${expected_commit}" test-run
expect_argument_failure "${repository}" "${tag}" relative-assets "${expected_commit}" test-run
expect_argument_failure "${repository}" "${tag}" "${ASSET_DIRECTORY}" deadbeef test-run
printf 'extra\n' >"${ASSET_DIRECTORY}/extra"
expect_argument_failure "${repository}" "${tag}" "${ASSET_DIRECTORY}" "${expected_commit}" test-run
rm -f -- "${ASSET_DIRECTORY}/extra"
printf 'hostile\n' >"${ASSET_DIRECTORY}/"$'hostile\nname'
expect_argument_failure "${repository}" "${tag}" "${ASSET_DIRECTORY}" "${expected_commit}" test-run
rm -f -- "${ASSET_DIRECTORY}/"$'hostile\nname'
printf 'hostile\n' >"${ASSET_DIRECTORY}/ hostile name "
expect_argument_failure "${repository}" "${tag}" "${ASSET_DIRECTORY}" "${expected_commit}" test-run
rm -f -- "${ASSET_DIRECTORY}/ hostile name "
expect_tool_failure find
expect_tool_failure sort
outside_archive=${test_root}/linked-archive
mv -- "${ASSET_DIRECTORY}/${archive}" "${outside_archive}"
ln -- "${outside_archive}" "${ASSET_DIRECTORY}/${archive}"
(
    cd -- "${ASSET_DIRECTORY}"
    sha256sum "${archive}" >"${checksum}"
)
expect_argument_failure "${repository}" "${tag}" "${ASSET_DIRECTORY}" "${expected_commit}" test-run
rm -f -- "${ASSET_DIRECTORY}/${archive}" "${outside_archive}"
new_assets validation
printf '%064d  %s\n' 0 "${archive}" >"${ASSET_DIRECTORY}/${checksum}"
expect_argument_failure "${repository}" "${tag}" "${ASSET_DIRECTORY}" "${expected_commit}" test-run

# A 404 creates a new draft with exactly the verified local pair.
new_state absent absent
new_assets absent
run_publisher
[[ $(<"${GH_STATE_DIR}/release-state") == draft ]]
assert_exact_remote_pair
assert_remote_matches_local

# Annotated tags are dereferenced through their tag object to the same commit.
new_state annotated absent
new_assets annotated
printf '%s\n' annotated >"${GH_STATE_DIR}/tag-kind"
run_publisher
assert_exact_remote_pair
assert_remote_matches_local

# A moved tag is refused both before and immediately after draft creation.
new_state moved-before-create absent
new_assets moved-before-create
printf '%s\n' "${moved_commit}" >"${GH_STATE_DIR}/tag-commit"
expect_failure
[[ $(<"${GH_STATE_DIR}/release-state") == absent ]]

new_state moved-during-create absent
new_assets moved-during-create
: >"${GH_STATE_DIR}/move-tag-during-create"
expect_failure
[[ $(<"${GH_STATE_DIR}/release-state") == draft ]]
assert_exact_remote_pair

# Collection failures are not mistaken for absence and cannot create a draft.
new_state release-list-error absent
new_assets release-list-error
: >"${GH_STATE_DIR}/fail-next-release-list"
expect_failure
[[ $(<"${GH_STATE_DIR}/release-state") == absent ]]
[[ -z $(find "${GH_STATE_DIR}/names" -mindepth 1 -print -quit) ]]

# Draft discovery is paginated and ignores unrelated release records.
new_state later-page draft
new_assets later-page
add_old_pair
printf '77\tfalse\tv9.9.9\n88\ttrue\tv0.0.9\n' \
    >"${GH_STATE_DIR}/release-list-prefix"
run_publisher
assert_exact_remote_pair
assert_remote_matches_local

# Duplicate exact tags and malformed collection records fail closed.
new_state duplicate-release-record draft
new_assets duplicate-release-record
add_old_pair
: >"${GH_STATE_DIR}/release-list-extra-match"
expect_failure
assert_old_pair_preserved

for malformed_record in $'invalid\ttrue\tv0.1.0' $'101\tmaybe\tv0.1.0' $'101\ttrue'; do
    new_state malformed-release-record draft
    new_assets malformed-release-record
    add_old_pair
    printf '%s\n' "${malformed_record}" >"${GH_STATE_DIR}/release-list-override"
    expect_failure
    assert_old_pair_preserved
done

# An existing draft with the canonical pair is transactionally replaced.
new_state update draft
new_assets update
add_old_pair
run_publisher
assert_exact_remote_pair
assert_remote_matches_local
assert_no_pending_assets

# Foreign assets make a draft ambiguous and are never deleted.
printf 'unrelated\n' >"${GH_STATE_DIR}/unrelated"
foreign_id=$(state_add_asset unrelated.txt "${GH_STATE_DIR}/unrelated")
expect_failure
assert_remote_matches_local
grep -qx unrelated "${GH_STATE_DIR}/assets/${foreign_id}"

# A published release is never mutated.
new_state published published
new_assets published
add_old_pair
expect_failure
assert_old_pair_preserved
if grep -Eq '^release (upload|create)|--method (DELETE|PATCH)' "${GH_STATE_DIR}/calls"; then
    echo "Published-release refusal performed a mutation" >&2
    exit 1
fi

# A failed ID-addressed upload is recovered on a rerun with the same nonce.
for failed_upload in 1 2; do
    new_state upload-failure-${failed_upload} draft
    new_assets upload-failure-${failed_upload}
    add_old_pair
    printf '%s\n' "${failed_upload}" >"${GH_STATE_DIR}/fail-upload-after-write"
    expect_failure
    assert_old_pair_content_preserved
    if [[ $(pending_asset_count) -ne 1 ]]; then
        echo "Upload failure did not leave exactly the untrusted starter asset" >&2
        exit 1
    fi
    if [[ ${failed_upload} -eq 1 ]]; then
        asset_id_for_name "${pending_archive}" >/dev/null || {
            echo "Failed upload's untrusted archive starter was deleted" >&2
            exit 1
        }
    else
        asset_id_for_name "${pending_checksum}" >/dev/null || {
            echo "Failed upload's untrusted checksum starter was deleted" >&2
            exit 1
        }
    fi
    run_publisher
    assert_exact_remote_pair
    assert_remote_matches_local
    assert_no_pending_assets
done

# The default nonce stays stable when GitHub reruns the same run at a new attempt.
new_state stable-default-nonce draft
new_assets stable-default-nonce
add_old_pair
printf '%s\n' 1 >"${GH_STATE_DIR}/fail-upload-after-write"
if run_publisher_default 1; then
    echo "First publication attempt must expose the interrupted upload" >&2
    exit 1
fi
asset_id_for_name "${archive}.pending-stable-run" >/dev/null
run_publisher_default 2
assert_exact_remote_pair
assert_remote_matches_local

# Empty and single-canonical-asset drafts are repairable subsets.
for partial in empty archive checksum; do
    new_state partial-${partial} draft
    new_assets partial-${partial}
    case ${partial} in
        archive)
            state_add_asset "${archive}" "${ASSET_DIRECTORY}/${archive}" >/dev/null
            ;;
        checksum)
            state_add_asset "${checksum}" "${ASSET_DIRECTORY}/${checksum}" >/dev/null
            ;;
    esac
    run_publisher
    assert_exact_remote_pair
    assert_remote_matches_local
done

# Matching current-nonce pending bytes may be cleaned only while identity holds.
for recovery_race in publish tag; do
    new_state recovery-race-${recovery_race} draft
    new_assets recovery-race-${recovery_race}
    add_old_pair
    pending_id=$(state_add_asset "${pending_archive}" "${ASSET_DIRECTORY}/${archive}")
    if [[ ${recovery_race} == publish ]]; then
        : >"${GH_STATE_DIR}/publish-after-pending-download"
    else
        : >"${GH_STATE_DIR}/move-tag-after-pending-download"
    fi
    expect_failure
    grep -qx "${pending_archive}" "${GH_STATE_DIR}/names/${pending_id}"
    if [[ ${recovery_race} == publish ]]; then
        [[ $(<"${GH_STATE_DIR}/release-state") == published ]]
    else
        [[ $(<"${GH_STATE_DIR}/tag-commit") == "${moved_commit}" ]]
    fi
done

# Asset-list failures are distinct from a missing pending name and fail closed.
new_state list-error draft
new_assets list-error
add_old_pair
: >"${GH_STATE_DIR}/fail-next-asset-list"
expect_failure
assert_old_pair_preserved
if grep -q 'uploads.github.com' "${GH_STATE_DIR}/calls"; then
    echo "Pending-name lookup error reached the upload endpoint" >&2
    exit 1
fi

# Mismatched and duplicate same-nonce pending names are preserved and refused.
for collision_count in 1 2; do
    new_state collision-${collision_count} draft
    new_assets collision-${collision_count}
    add_old_pair
    printf 'collision\n' >"${GH_STATE_DIR}/collision"
    for ((collision_index = 0; collision_index < collision_count; collision_index += 1)); do
        state_add_asset "${pending_archive}" "${GH_STATE_DIR}/collision" >/dev/null
    done
    expect_failure
    assert_old_pair_content_preserved
    asset_id_for_name "${pending_archive}" >/dev/null
    if grep -q 'uploads.github.com' "${GH_STATE_DIR}/calls"; then
        echo "Pending-name collision reached the upload endpoint" >&2
        exit 1
    fi
done

# Pending assets from a different run nonce are foreign and preserved.
new_state other-nonce draft
new_assets other-nonce
add_old_pair
other_pending_id=$(state_add_asset "${archive}.pending-other-run" "${ASSET_DIRECTORY}/${archive}")
expect_failure
assert_old_pair_content_preserved
grep -qx "${archive}.pending-other-run" "${GH_STATE_DIR}/names/${other_pending_id}"

# Duplicate canonical names are ambiguous and preserved.
new_state duplicate-canonical draft
new_assets duplicate-canonical
add_old_pair
duplicate_id=$(state_add_asset "${archive}" "${ASSET_DIRECTORY}/${archive}")
expect_failure
grep -qx "${archive}" "${GH_STATE_DIR}/names/${duplicate_id}"

# A draft becoming published before mutation is refused without changing assets.
new_state publish-race draft
new_assets publish-race
add_old_pair
: >"${GH_STATE_DIR}/publish-on-next-check"
expect_failure
[[ $(<"${GH_STATE_DIR}/release-state") == published ]]
assert_old_pair_preserved

# A tag move before the first staged upload is detected without mutation.
new_state moved-before-upload draft
new_assets moved-before-upload
add_old_pair
: >"${GH_STATE_DIR}/move-tag-on-release-check"
expect_failure
assert_old_pair_preserved
if grep -q 'uploads.github.com' "${GH_STATE_DIR}/calls"; then
    echo "Moved tag reached the upload endpoint" >&2
    exit 1
fi

# Moving the tag after staging but before replacement prevents old-asset deletion.
new_state moved-before-mutation draft
new_assets moved-before-mutation
add_old_pair
: >"${GH_STATE_DIR}/move-tag-after-uploads"
expect_failure
assert_old_pair_content_preserved
if grep -q -- '--method DELETE\|--method PATCH' "${GH_STATE_DIR}/calls"; then
    echo "Moved tag allowed replacement mutation" >&2
    exit 1
fi

# Downloaded remote bytes must match before replacement begins.
new_state corrupt-remote draft
new_assets corrupt-remote
add_old_pair
: >"${GH_STATE_DIR}/corrupt-next-download"
expect_failure
assert_old_pair_preserved
assert_no_pending_assets

# A final unexpected remote asset is detected rather than reported as success.
new_state final-mismatch draft
new_assets final-mismatch
add_old_pair
printf 'injected\n' >"${GH_STATE_DIR}/inject-final-mismatch"
expect_failure
if [[ -z $(asset_id_for_name unexpected.txt) ]]; then
    echo "Final mismatch fixture did not inject its unexpected asset" >&2
    exit 1
fi

# Final remote bytes are re-downloaded and checked after the rename sequence.
new_state final-corrupt draft
new_assets final-corrupt
add_old_pair
: >"${GH_STATE_DIR}/corrupt-final-archive"
expect_failure
assert_exact_remote_pair

# A tag move during final remote verification is detected.
new_state moved-during-final draft
new_assets moved-during-final
add_old_pair
printf '%s\n' 3 >"${GH_STATE_DIR}/move-tag-on-download-count"
expect_failure
assert_exact_remote_pair

# The fake rejects draft creation without --repo and accepts asset APIs only
# when their endpoint contains the explicit expected repository and release ID.
