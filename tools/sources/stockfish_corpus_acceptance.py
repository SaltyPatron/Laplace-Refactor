#!/usr/bin/env python3
"""Prove complete configured Stockfish source admission by the activated product.

This orchestrates the generic source CLI. Native admission and native exact
readback own content, grammar structure, identities and persistence semantics.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import pwd
import tempfile
import shutil
import signal
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from dependencies import chess_tools
from sources import verified_git
from delivery import product_activation as activation
from delivery import product_activation_runner as runner
from delivery import receipt_estate
from product import repository_inputs, build_workspace_retention
import admit_source_guard

SCHEMA = 'laplace.stockfish-corpus-acceptance/v1'
HEX = re.compile(r'[0-9a-f]{64}\Z')
READBACK_DIGEST_FIELDS = ('source_profile_id','structural_receipt_id','source_binding_id',
    'recipe_id','materialization_receipt_id','provider_fingerprint','readset_fingerprint','output_fingerprint')
READBACK_COUNT_FIELDS = ('artifact_index','output_bytes','resolved_nodes','trajectory_carriers',
    'codepoint_count','maximum_depth','verified_witnesses','database_operations','version')
READBACK_FIELDS = {'path','sha256','root_content_id',*READBACK_DIGEST_FIELDS,*READBACK_COUNT_FIELDS}
EXECUTION_IDENTITY_FIELDS = ('execution_witness_fingerprint','structural_execution_receipt_id',
    'historical_structural_receipt_id')
EXECUTION_COUNT_FIELDS = ('structural_execution_observation_count','structural_execution_receipt_count')


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def load(path: Path, maximum: int = 32 * 1024 * 1024) -> dict:
    require(path.is_file() and not path.is_symlink() and path.stat().st_size <= maximum,
            'missing, linked or oversized evidence: ' + str(path))
    value = json.loads(path.read_text(encoding='utf-8'))
    require(isinstance(value, dict), 'evidence must be a JSON object: ' + str(path))
    return value


def save(path: Path, value: dict) -> None:
    data = json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2).encode() + b'\n'
    temporary = path.with_name(path.name + '.pending')
    with temporary.open('xb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)
    descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def number(value: object, label: str, minimum: int = 0) -> int:
    require(type(value) is int and value >= minimum, 'invalid count: ' + label)
    return value


def validate_package_binding(expected_sha: str, package_id: str, manifest: dict,
                             installation: dict, aggregate: dict,
                             expected_build_fingerprint: str) -> None:
    require(re.fullmatch(r'[0-9a-f]{40}', expected_sha) is not None,
            'expected SHA must identify one exact main commit')
    require(HEX.fullmatch(package_id) is not None, 'invalid active package identity')
    require(manifest.get('package_id') == installation.get('package_id') ==
            aggregate.get('package_id') == package_id, 'activation/package identities differ')
    # Package reuse preserves its original build commit/tree. Deployment keeps
    # the requested commit, while the existing package-input owner binds content.
    source = manifest.get('laplace', {})
    require(isinstance(source, dict) and
            re.fullmatch(r'[0-9a-f]{40}', str(source.get('repository_commit'))) is not None and
            re.fullmatch(r'[0-9a-f]{40}', str(source.get('repository_tree'))) is not None and
            HEX.fullmatch(str(source.get('repository_build_fingerprint'))) is not None and
            HEX.fullmatch(str(expected_build_fingerprint)) is not None,
            'activated package source provenance is missing or malformed')
    require(source['repository_build_fingerprint'] == expected_build_fingerprint and
            aggregate.get('repository_commit') == expected_sha,
            'activated package is stale or belongs to different source inputs or deployment')
    require(aggregate.get('schema') == runner.RESULT_SCHEMA and
            aggregate.get('result_sha256') == runner.document_identity(aggregate, 'result_sha256'),
            'runner terminal receipt identity differs')
    require(aggregate.get('execution_owner') == runner.RUNNER_USER and
            aggregate.get('root_product_executor') is False and
            aggregate.get('postgresql_lifecycle_provider') in runner.clusterctl.LIFECYCLE_PROVIDERS,
            'activation did not use the current runner-owned lifecycle')
    activation.validate_product_terminal_result(aggregate)
    runner.validate_package_installation(installation, manifest, Path(installation['source_physical_root']))
    require(aggregate.get('package_installation_receipt_sha256') ==
            installation['installation_receipt_sha256'], 'installation is not bound by activation')


def retain_activation(source: Path, expected_sha: str, receipt_root: Path) -> Path:
    """Publish exact validated runner bytes using the existing receipt estate owner."""
    aggregate = load(source)
    require(re.fullmatch(r'[0-9a-f]{40}', expected_sha) is not None and
            aggregate.get('repository_commit') == expected_sha and
            HEX.fullmatch(str(aggregate.get('package_id'))) is not None,
            'retained activation source/package address differs')
    require(aggregate.get('schema') == runner.RESULT_SCHEMA and
            aggregate.get('result_sha256') == runner.document_identity(aggregate, 'result_sha256') and
            aggregate.get('execution_owner') == runner.RUNNER_USER and
            aggregate.get('root_product_executor') is False and
            aggregate.get('postgresql_lifecycle_provider') in runner.clusterctl.LIFECYCLE_PROVIDERS,
            'retained activation is not an authenticated runner result')
    activation.validate_product_terminal_result(aggregate)
    # The result digest distinguishes repeat activations without overwriting any
    # prior receipt at the same source/package address.
    destination = receipt_root/'product-activation'/expected_sha/aggregate['package_id']/(aggregate['result_sha256']+'.json')
    descriptor, name = tempfile.mkstemp(prefix='stockfish-activation-', dir=source.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(source.read_bytes())
            stream.flush()
            os.fsync(stream.fileno())
        require(load(temporary) == aggregate, 'activation changed during exact retention')
        receipt_estate.publish_exact(temporary, destination, os.geteuid(), os.getegid(), False)
    finally:
        temporary.unlink(missing_ok=True)
    return destination


def resolve_activation_receipt(receipt_root: Path, package_id: str, name: str) -> tuple[Path, dict]:
    """Read one producer receipt before or after the estate owner's exact move."""
    require(HEX.fullmatch(package_id) is not None and name in {
        'unicode-product-activation.json', 'highway-product-activation.json',
        'highway-committed-revalidation.json'}, 'invalid native receipt address')
    canonical = receipt_root/'cluster-activation'/package_id/name
    for parent in (receipt_root, canonical.parent.parent, canonical.parent):
        if parent.exists() or parent.is_symlink():
            require(parent.is_dir() and not parent.is_symlink(),
                    'native receipt directory is unsafe: ' + str(parent))
    present = []
    for path in (receipt_root/name, canonical):
        if path.exists() or path.is_symlink():
            document = load(path)
            require(document.get('package_id') == package_id,
                    'native receipt belongs to another package: ' + str(path))
            present.append((path, document))
    require(bool(present), 'native activation receipt is absent: ' + name)
    if len(present) == 2:
        require(present[0][0].read_bytes() == present[1][0].read_bytes(),
                'producer and canonical native receipt bytes conflict: ' + name)
    return present[-1]  # Prefer the retained canonical copy when both are byte-exact.


def observe_native_receipts(receipt_root: Path, expected_sha: str, package_id: str,
                            manifest: dict, installation: dict, plan: dict,
                            cluster_result: dict,
                            expected_build_fingerprint: str) -> tuple[dict, dict, list, dict]:
    """Bind retained native bytes to unchanged authenticated producer paths."""
    evidence = receipt_root/'cluster-activation'/package_id
    unicode_path = receipt_root/'unicode-product-activation.json'
    unicode_stored, unicode = resolve_activation_receipt(receipt_root, package_id, unicode_path.name)
    runner.validate_unicode_result(unicode, package_id)
    aggregate_paths = sorted((receipt_root/'product-activation'/expected_sha/package_id).glob('*.json'))
    require(len(aggregate_paths) <= 1024, 'activation receipt inventory exceeds its declared boundary')
    matches = []
    for path in aggregate_paths:
        aggregate = load(path)
        validate_package_binding(expected_sha, package_id, manifest, installation, aggregate,
                                 expected_build_fingerprint)
        require(path.stem == aggregate['result_sha256'], 'activation receipt address differs from its bytes')
        if (aggregate.get('cluster_activation_receipt_sha256') != cluster_result['activation_receipt_sha256'] or
                aggregate.get('unicode_activation_receipt_sha256') != unicode['receipt_sha256']):
            continue  # A retained earlier activation is not the currently observed generation.
        highway_path = receipt_root/('highway-committed-revalidation.json'
            if aggregate['phase'] == activation.HIGHWAY_REVALIDATION_PHASE else 'highway-product-activation.json')
        highway_stored, highway = resolve_activation_receipt(receipt_root, package_id, highway_path.name)
        runner.validate_highway_result(highway, package_id)
        validate_native_activation_links(plan,cluster_result,unicode,highway)
        require(aggregate.get('cluster_result') == str(evidence/'activation-result.json') and
                aggregate.get('unicode_result') == str(unicode_path) and
                aggregate.get('highway_result') == str(highway_path),
                'terminal runner receipt paths differ from current native activation')
        if not all(aggregate.get(k) == v for k,v in activation.highway_aggregate_fields(highway).items()):
            continue
        matches.append((path, aggregate, highway, highway_stored))
    require(bool(matches), 'no successful runner activation receipt binds this main commit and current package')
    highway = matches[0][2]
    require(all(item[2] == highway for item in matches), 'current activation receipts disagree on Highway')
    require(str(unicode.get('system_identifier')) == str(highway.get('system_identifier')) ==
            str(cluster_result['system_identifier']), 'native activation cluster identities differ')
    return unicode, highway, matches, {'unicode': str(unicode_stored), 'highway': str(matches[0][3])}


def resolve_installed_package_metadata(product: dict, installation: dict, plan: dict,
                                       package_id: str) -> dict:
    """Bind actual installation/plan identities before selecting retained bytes."""
    stage = Path(installation['source_physical_root'])
    relative = stage.relative_to(Path(product['package_stage_root']))
    require(len(relative.parts) == 2 and HEX.fullmatch(relative.parts[0]) is not None and
            relative.parts[1] == 'root', 'installed package omits its addressed build plan')
    require(installation.get('package_id') == plan.get('package_id') == package_id and
            installation.get('package_manifest_sha256') == plan.get('package_manifest_sha256'),
            'installed package/activation plan manifest differs')
    metadata = build_workspace_retention.resolve_package_metadata(
        Path(product['package_manifest_root']), Path(product['package_stage_root']),
        relative.parts[0], package_id, plan['package_manifest_sha256'])
    return metadata


def observe_activation(expected_sha: str, output: Path) -> dict:
    runner.require_runner()
    expected_build_fingerprint = repository_inputs.repository_build_fingerprint_at_commit(ROOT, expected_sha)
    gateway = load(ROOT/'contracts/product-activation-gateway.json')
    cluster_contract = load(ROOT/'contracts/postgresql-cluster.json')
    cluster = runner.clusterctl
    product = gateway['product']
    active = Path(cluster_contract['package']['active_link'])
    require(active.is_symlink(), 'the installed product selector is unavailable')
    release = active.resolve(strict=True)
    package_id = release.name
    require(HEX.fullmatch(package_id) is not None and
            release == Path(cluster_contract['package']['release_root'])/package_id and
            os.readlink(active) == 'releases/'+package_id,
            'active product pointer does not select an addressed release')
    receipt_root = Path(cluster_contract['instance']['receipt_directory'])
    evidence = receipt_root/'cluster-activation'/package_id
    installation = load(evidence/'package-installation.json')
    cluster_result = load(evidence/'activation-result.json')
    plan_path = runner.validate_cluster_result(cluster_result, package_id)
    plan = load(plan_path)
    cluster.validate_plan(plan, cluster_contract)
    require(plan['plan_sha256'] == cluster_result['plan_sha256'],
            'activated cluster plan differs from authenticated activation')
    metadata = resolve_installed_package_metadata(product, installation, plan, package_id)
    manifest_path = Path(metadata['manifest_path'])
    manifest = metadata['manifest']
    runtime = Path(plan['runtime_link'])
    require(runtime.is_symlink() and os.readlink(runtime) == '../releases/'+package_id and
            runtime.resolve(strict=True) == release, 'live runtime selector differs from active package')
    status = cluster.verify_package(manifest, cluster_contract, Path('/'))
    require(status.verified, 'installed package verification failed: '+status.reason)
    validate_loaded_plan(plan, manifest, cluster_contract, status.files, release)
    require(status.manifest_sha256 == installation['package_manifest_sha256'] ==
            plan['package_manifest_sha256'] and plan['package_id'] == package_id,
            'installed package/activation plan manifest differs')
    unicode, highway, matches, native_receipt_paths = observe_native_receipts(
        receipt_root, expected_sha, package_id, manifest, installation, plan, cluster_result,
        expected_build_fingerprint)
    loaded = cluster.observe_loaded_live(plan, cluster_contract, Path('/'))
    cluster.verify_loaded(plan, cluster_contract, loaded)
    require(str(loaded['system_identifier']) == str(cluster_result['system_identifier']),
            'live PostgreSQL identity differs from activation')
    require(pwd.getpwnam(cluster_contract['instance']['os_user']).pw_uid == os.geteuid() ==
            Path('/proc',str(loaded['postmaster_pid'])).stat().st_uid,
            'acceptance and actual PostgreSQL do not share the declared runner identity')
    cli = release/'bin/laplace-admit-source'
    require(cli.is_file() and not cli.is_symlink() and os.access(cli, os.X_OK),
            'installed generic source admission CLI is unavailable')
    require(sha(manifest_path) == metadata['manifest_sha256'] and
            sha(Path(metadata['receipt_path'])) == metadata['receipt_sha256'],
            'selected package metadata changed during activation observation')
    service_owner = cluster.service_lifecycle().observe_selected(cluster, plan, loaded)
    if cluster_result.get('lifecycle_provider') == 'systemd-system':
        cluster.lifecycle_result_fields(plan, loaded)
    snapshot = {'package_id': package_id, 'repository_commit': expected_sha,
        'repository_build_fingerprint': expected_build_fingerprint,
        'release': str(release), 'cli': str(cli), 'cli_sha256': sha(cli),
        'manifest_path': str(manifest_path), 'manifest_sha256': metadata['manifest_sha256'],
        'package_metadata': {key: metadata[key] for key in (
            'selection', 'plan_id', 'receipt_path', 'receipt_sha256', 'original_manifest_path',
            'build_metadata')},
        'manifest': manifest, 'installation': installation,
        'runner_receipts': [{'path': str(p), 'document': d} for p,d,_,_ in matches],
        'native_receipt_paths': native_receipt_paths,
        'cluster_activation': cluster_result, 'unicode_activation': unicode,
        'highway_activation': highway, 'cluster_plan': plan, 'loaded': loaded,
        'postgresql_service_owner': service_owner}
    save(output, snapshot)
    return snapshot


def validate_native_activation_links(plan: dict, cluster: dict, unicode: dict, highway: dict) -> None:
    require(cluster.get('plan_sha256') == plan['plan_sha256'],
            'activated cluster plan differs from authenticated activation')
    for label,receipt in (('Unicode',unicode),('Highway',highway)):
        require(receipt.get('cluster_plan_sha256') == plan['plan_sha256'] and
                receipt.get('cluster_activation_receipt_sha256') == cluster['activation_receipt_sha256'],
                label+' receipt belongs to another cluster activation')
    require(highway.get('unicode_activation_receipt_sha256') == unicode['receipt_sha256'],
            'Highway receipt belongs to another Unicode activation')


def validate_loaded_plan(plan: dict, manifest: dict, contract: dict, files: dict, release: Path) -> None:
    require(plan['package_root'] == manifest['root'] == str(release),
            'loaded plan selects another package root')
    require(plan['instance'] == contract['instance'],
            'loaded plan selects another declared PostgreSQL instance')
    expected = [{'path':str(release/relative),'sha256':files[relative]['sha256']}
                for relative in contract['package']['required_loaded_objects']]
    require(plan['required_loaded_objects'] == expected,
            'loaded plan omitted or changed required package objects')


def verify_provider_access(receipt: dict, active: dict) -> dict:
    library = Path(receipt['library']['path'])
    require(library.is_file() and not library.is_symlink() and
            sha(library) == receipt['library']['sha256'] and
            library.stat().st_size == receipt['library']['byte_count'],
            'qualified grammar provider bytes differ')
    require(Path('/proc',str(active['loaded']['postmaster_pid'])).stat().st_uid == os.geteuid() and
            os.access(library,os.R_OK) and all(os.access(p,os.X_OK) for p in library.parents),
            'actual PostgreSQL owner cannot traverse/read the qualified grammar provider')
    return {'provider_path':str(library),'provider_sha256':sha(library),
        'postgresql_os_uid':os.geteuid(),'provider_readable_and_parents_traversable':True}


def select_runtime_root(active: dict, explicit: Path | None) -> dict:
    closure = active['manifest'].get('provenance',{}).get('build_input_closure',{})
    require(closure.get('schema') == 'laplace.product-build-input-closure-receipt/v1' and
            closure.get('complete') is True,
            'activated package omits its complete runtime build-input receipt')
    retained = closure.get('build_input_roots',{}).get('tree-sitter',{})
    require(isinstance(retained,dict) and HEX.fullmatch(str(retained.get('tree_sha256'))) is not None,
            'activated package omits its Tree-sitter input identity')
    for field in ('file_count','symlink_count','directory_count','total_file_bytes'):
        number(retained.get(field), 'runtime build input '+field)
    original = Path(retained.get('path',''))
    require(original.is_absolute(), 'activated package runtime input path is not absolute')
    selected = explicit if explicit is not None else original
    require(selected.is_absolute() and selected.is_dir() and not selected.is_symlink(),
            'selected Tree-sitter runtime checkout is unavailable or not a physical absolute directory')
    return {'schema':'laplace.stockfish-corpus-runtime-selection/v1',
        'package_id':active['package_id'],'runtime_root':str(selected),
        'selection':'explicit-runtime-root' if explicit is not None else 'activated-package-build-input',
        'activated_package_build_input':retained,
        'qualification':'selected checkout still requires exact locked revision, archive and tracked-byte verification'}


def stable_activation(snapshot: dict) -> dict:
    return {key: snapshot[key] for key in ('package_id','repository_commit','release',
        'cli_sha256','manifest_sha256','installation','cluster_activation',
        'unicode_activation','highway_activation')}


def validate_readback(result: dict, manifest: dict, active: dict) -> None:
    require(result.get('schema') == 'laplace.admit-source/v1' and
            result.get('phase') == 'source-admitted-and-exactly-read-back' and
            result.get('profile') == 'verified-git-code' and
            result.get('active_package_id') == active['package_id'] and
            result.get('tool_release') == active['release'] and
            result.get('executable_semantics_verified') is False,
            'installed admission did not return an exact current-package observation')
    require(result.get('verified_git_input', {}).get('manifest') == manifest,
            'native admission used another source observation')
    admission = result['admission']
    require(number(admission.get('testimony_count'),'testimony_count') == 0 and
            number(admission.get('evidence_node_count'),'evidence_node_count') == 0 and
            number(result['persisted_profile']['claim_count'],'claim_count') == 0,
            'observing source code invented semantic testimony')
    readback = result['readback']
    artifacts = manifest['artifacts']
    witness_fingerprint = readback.get('structural_witness_fingerprint')
    require(isinstance(witness_fingerprint,str) and HEX.fullmatch(witness_fingerprint) is not None,
            'source readback omitted a verified structural witness fingerprint')
    for field in EXECUTION_IDENTITY_FIELDS:
        identity = readback.get(field)
        require(isinstance(identity,str) and HEX.fullmatch(identity) is not None and
                identity != '0'*64, 'source readback omitted a verified execution identity: ' + field)
    metrics = result.get('execution_metrics')
    execution = metrics.get('last') if isinstance(metrics,dict) else None
    require(isinstance(execution,dict) and execution.get('valid') is True and
            execution.get('structural_execution_receipt_id') == readback['structural_execution_receipt_id'],
            'source readback does not bind the admitted current execution receipt')
    require(readback.get('schema') == 'laplace.verified-git-source-readback/v1' and
            readback.get('all_artifacts_exact') is True and
            number(readback.get('structural_receipt_count'),'structural_receipt_count') == 1 and
            number(readback.get('verified_file_count'),'verified_file_count') == manifest['file_count'] == len(artifacts) and
            number(readback.get('verified_byte_count'),'verified_byte_count') == manifest['byte_count'],
            'full source readback denominators differ')
    records = readback.get('records')
    require(isinstance(records, list) and len(records) == len(artifacts),
            'source readback omitted or repeated selected files')
    for index, (expected, actual) in enumerate(zip(artifacts, records)):
        require(isinstance(actual,dict) and set(actual) == READBACK_FIELDS,
                'native readback metadata fields differ from the complete result contract')
        require(actual.get('path') == expected['path'] and number(actual['artifact_index'],'artifact_index') == index and
                actual.get('sha256') == expected['sha256'] and
                number(actual['output_bytes'],'output_bytes') == expected['byte_count'] and
                actual.get('source_profile_id') == admission['profile_id'],
                'source readback changed bytes, path, order or profile: ' + expected['path'])
        for field in ('root_content_id',*READBACK_DIGEST_FIELDS):
            identity=actual.get(field)
            width=32 if field=='root_content_id' else 64
            require(isinstance(identity,str) and re.fullmatch(r'\\x[0-9a-f]{'+str(width)+'}',identity) is not None and
                    set(identity[2:])!={'0'}, 'readback omitted a native root or receipt identity: '+field)
        for field in READBACK_COUNT_FIELDS:
            number(actual[field],field,1 if field in ('output_bytes','resolved_nodes','codepoint_count','verified_witnesses') else 0)
        require(actual['version']==1 and actual['maximum_depth']<=256 and
                actual['codepoint_count']<=actual['output_bytes'],
                'native readback version, depth or UTF-8 count is inconsistent')
        require(actual['verified_witnesses']==readback.get('structural_witness_count') and
                actual['structural_receipt_id']==records[0]['structural_receipt_id'] and
                actual['recipe_id']==records[0]['recipe_id'],
                'native readback rows do not share the verified structural receipt and recipe')
        require(actual['structural_receipt_id'] == '\\x'+readback['structural_execution_receipt_id'],
                'native readback row does not bind the current structural receipt')
    require(len({row['source_binding_id'] for row in records})==len(records),
            'native readback repeated a distinct artifact source binding')
    for field in ('source_occurrence_count','structural_witness_count',*EXECUTION_COUNT_FIELDS):
        number(readback.get(field), field, 1)
    counts = readback.get('database_row_counts')
    require(isinstance(counts, dict) and set(counts) == {'entity','physicality','attestation'},
            'fresh postcommit substrate counts are unavailable')
    for field,value in counts.items(): number(value, field)


def verify_repeat(first: dict, second: dict, manifest: dict, active: dict) -> dict:
    for result in (first,second): validate_readback(result, manifest, active)
    for field in ('active_package_id','tool_release','geometry_epoch','perfcache_epoch',
                  'numeric_epoch','native_source_fingerprint','native_reconstruction_fingerprint',
                  'verified_git_input','persisted_profile'):
        require(first[field] == second[field], 'repeat changed canonical/context identity: ' + field)
    for field in ('profile_id','source_fingerprint','reconstruction_fingerprint',
                  'source_profile_receipt_id','root_entity_id','root_physicality_id'):
        require(first['admission'][field] == second['admission'][field],
                'repeat changed native source identity: ' + field)
    a,b = first['readback'],second['readback']
    require(a['structural_witness_fingerprint'] == b['structural_witness_fingerprint'],
            'repeat changed the semantic structural witness fingerprint')
    for field in EXECUTION_IDENTITY_FIELDS:
        require(a[field] == b[field], 'repeat changed current execution identity: ' + field)
    # Identical admission reuses its exact current execution and historical anchor.
    # Physical readback work and artifact bindings may vary with canonical reuse;
    # both native results still bind that same execution and exact source content.
    root_fields = ('path','artifact_index','source_profile_id','root_content_id',
                   'recipe_id','sha256','output_bytes',
                   'output_fingerprint','codepoint_count')
    for x,y in zip(a['records'],b['records']):
        require(all(key in x and key in y and x[key] == y[key] for key in root_fields),
                'repeat changed an exact source root')
    for field in ('source_occurrence_count','structural_witness_count','database_row_counts',*EXECUTION_COUNT_FIELDS):
        require(a[field] == b[field], 'repeat amplified persistent state: ' + field)
    return {'verified_file_count':manifest['file_count'], 'verified_byte_count':manifest['byte_count'],
        'source_occurrence_delta':0, 'structural_witness_delta':0,
        'structural_execution_observation_delta':0, 'structural_execution_receipt_delta':0,
        **{field:b[field] for field in EXECUTION_IDENTITY_FIELDS},
        'substrate_row_deltas':{field:b['database_row_counts'][field]-a['database_row_counts'][field]
                                for field in ('entity','physicality','attestation')}}


def run_command(command: list[str], name: str, output: Path, deadline: float, report: dict) -> dict:
    remaining = deadline-time.monotonic()
    require(remaining > 0, 'acceptance deadline exhausted before ' + name)
    stdout,stderr = output/(name+'.stdout.log'),output/(name+'.stderr.log')
    observation = {'name':name,'command':command,'started_at_unix':time.time(),
                   'status':'submitted','stdout':str(stdout),'stderr':str(stderr)}
    report['commands'].append(observation)
    save(output/'result.json',report)
    with stdout.open('xb') as out,stderr.open('xb') as err:
        process = subprocess.Popen(command,stdout=out,stderr=err,start_new_session=True)
        try:
            process.wait(timeout=remaining)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid,signal.SIGKILL)
            process.wait()
            observation['status']='deadline-exhausted-submission-outcome-unknown'
            raise ValueError('acceptance deadline exhausted during ' + name)
        finally:
            out.flush(); err.flush(); os.fsync(out.fileno()); os.fsync(err.fileno())
            observation.update({'finished_at_unix':time.time(),'exit_code':process.returncode,
                'stdout_sha256':sha(stdout),'stderr_sha256':sha(stderr)})
    observation['status']='completed' if process.returncode==0 else 'failed'
    require(process.returncode==0, name+' failed; exact command output is retained')
    return load(stdout)


def execute(args: argparse.Namespace) -> dict:
    output=args.output.absolute()
    configured=load(args.chess_prefix/'current.json')
    configured_source=Path(configured['tools']['stockfish']['source']).resolve(strict=True)
    # Observe all candidates before creating evidence: the requested or explicit
    # checkout can differ from the currently built source.
    observed_selection=chess_tools.select_stockfish_source(args.chess_prefix,getattr(args,'stockfish_source',None))
    selection_path=getattr(args,'source_selection',None)
    selection=load(selection_path) if selection_path is not None else observed_selection
    protected={configured_source}
    for candidate in (selection,observed_selection):
        if candidate.get('selected_source'):
            protected.add(Path(candidate['selected_source']).resolve())
        for field in ('requested_checkout','configured_checkout','selected_checkout'):
            checkout=candidate.get(field)
            if checkout and checkout.get('available') and checkout.get('source'):
                protected.add(Path(checkout['source']).resolve())
    require(all(source not in output.resolve().parents and output.resolve()!=source for source in protected),
            'evidence output must be outside the upstream checkout')
    output.mkdir(parents=True,exist_ok=False)
    report={'schema':SCHEMA,'status':'running','expected_repository_commit':args.expected_sha,
            'commands':[],'canonical_corpus_admission_completed':False,
            'zero_amplification_repeat_proven':False,'executable_semantics_verified':False}
    save(output/'result.json',report)
    deadline=time.monotonic()+args.timeout
    try:
        save(output/'stockfish-source-selection.json',selection)
        save(output/'stockfish-source-candidates.json',observed_selection)
        selection_verified=chess_tools.verify_stockfish_selection(selection,args.chess_prefix,getattr(args,'stockfish_source',None))
        save(output/'stockfish-source-selection-verified.json',selection_verified)
        before=observe_activation(args.expected_sha,output/'activation-before.json')
        selected,artifacts=chess_tools.configuration()
        installation=chess_tools.verify_installation(args.chess_prefix,selected,artifacts,only='stockfish')
        tool=installation['tools']['stockfish']
        require(tool.get('source_verifier_sha256') == sha(ROOT/'tools/dependencies/git_checkout.py') and
                isinstance(tool.get('tracked_source'),dict),
                'Stockfish needs a build receipt with current exact committed-byte provenance')
        source=Path(tool['source']).resolve(strict=True)
        require(str(source)==selection_verified['selected_source'],
                'Stockfish installation changed after source selection verification')
        require(source not in output.resolve().parents and output.resolve()!=source,
                'evidence output must be outside the upstream checkout')
        lock=load(ROOT/'dependencies/lock.json')['dependencies']
        require(tool['executable']==str(source/'src/stockfish'),
                'Stockfish executable is not the verified direct source build')
        observed,files=verified_git.observe(source,lock['stockfish']['upstream'],lock['stockfish']['revision'],
            expected_archive_sha256=lock['stockfish']['git_archive_sha256'])
        tracked=tool['tracked_source']
        require(tracked['commit']==observed['commit'] and tracked['tree']==observed['tree'] and
                tracked['file_count']==observed['file_count'] and tracked['byte_count']==observed['byte_count'] and
                sorted(tracked['artifacts'],key=lambda item:item['path'])==
                    [{k:item[k] for k in ('path','git_mode','git_blob','sha256','byte_count')}
                     for item in observed['artifacts']],
                'Stockfish build provenance differs from the complete observed Git corpus')
        estate=admit_source_guard.configured_estate_root()
        frozen=estate/'acceptance'/'stockfish'/uuid.uuid4().hex
        manifest_path=verified_git.freeze(observed,files,frozen)
        shutil.copytree(frozen,output/'source-snapshot')
        network=artifacts[selected['releases']['stockfish']['network']]
        network_path=source/'src'/network['filename']
        build_log=Path(tool['build_log'])
        require(build_log.is_file(),'Stockfish retained build log is unavailable')
        shutil.copyfile(build_log,output/'stockfish-build.log')
        provenance={'installation':installation,'current_receipt_sha256':sha(args.chess_prefix/'current.json'),
            'source_manifest':str(manifest_path),'source_manifest_sha256':sha(manifest_path),
            'executable_sha256':sha(Path(tool['executable'])),'build_log_sha256':sha(build_log),
            'nnue':{'path':str(network_path),'sha256':sha(network_path),'bytes':network_path.stat().st_size}}
        require(sha(output/'stockfish-build.log')==provenance['build_log_sha256'],
                'Stockfish build log changed while being retained')
        save(output/'stockfish-provenance.json',provenance)
        release=Path(before['release'])
        native_lock=release/'share/laplace/source-contracts/native-dependency-lock.json'
        grammar_lock=release/'share/laplace/source-contracts/grammar-lock.json'
        require(sha(native_lock)==sha(ROOT/'dependencies/lock.json') and
                sha(grammar_lock)==sha(ROOT/'dependencies/tree-sitter-grammars.lock.json'),
                'installed source grammar/runtime locks differ from accepted repository')
        work=Path('/build/laplace/work')/('stockfish-corpus-'+uuid.uuid4().hex)
        work.mkdir(mode=0o750)
        runtime_selection=select_runtime_root(before,args.runtime_root)
        save(output/'runtime-selection.json',runtime_selection)
        runtime=Path(runtime_selection['runtime_root'])
        qualifier=[sys.executable,str(ROOT/'tools/sources/qualify_grammar.py'),'--acquire',
            '--grammar-root',str(work/'tree-sitter-cpp'),'--runtime-root',str(runtime),
            '--grammar-lock',str(grammar_lock),'--dependency-lock',str(native_lock),
            '--grammar','tree-sitter-cpp','--output',str(output/'grammar')]
        run_command(qualifier,'grammar-qualification',output,deadline,report)
        grammar=output/'grammar/build-receipt.json'
        save(output/'grammar-access.json',verify_provider_access(load(grammar),before))
        command=[before['cli'],'verified-git-code',str(frozen/'files'),
            '--git-manifest',str(manifest_path),'--grammar-receipt',str(grammar),
            '--active',str(release),'--pretty']
        # Recheck immediately before a command that can write canonical state.
        require(chess_tools.verify_stockfish_selection(selection,args.chess_prefix,getattr(args,'stockfish_source',None))==selection_verified,
                'Stockfish selected source/build changed before corpus admission')
        first=run_command(command,'admission',output,deadline,report)
        validate_readback(first,observed,before)
        report['canonical_corpus_admission_completed']=True
        save(output/'result.json',report)
        require(chess_tools.verify_stockfish_selection(selection,args.chess_prefix,getattr(args,'stockfish_source',None))==selection_verified,
                'Stockfish selected source/build changed before corpus repeat')
        second=run_command(command,'repeat',output,deadline,report)
        repeat=verify_repeat(first,second,observed,before)
        after=observe_activation(args.expected_sha,output/'activation-after.json')
        require(stable_activation(before)==stable_activation(after),'activated product changed during acceptance')
        final=chess_tools.verify_installation(args.chess_prefix,selected,artifacts,only='stockfish')
        require(final==installation and sha(Path(tool['executable']))==provenance['executable_sha256'] and
                sha(network_path)==provenance['nnue']['sha256'] and
                sha(args.chess_prefix/'current.json')==provenance['current_receipt_sha256'] and
                sha(build_log)==provenance['build_log_sha256'],
                'Stockfish source/build/executable/network changed during acceptance')
        require(time.monotonic()<=deadline,'acceptance deadline exhausted before final evidence')
        report.update({'status':'passed','zero_amplification_repeat_proven':True,
            'package_id':before['package_id'],'source_commit':observed['commit'],
            'source_tree':observed['tree'],'source_manifest_sha256':sha(manifest_path),
            'cpp_file_count':observed['cpp_file_count'],'repeat':repeat,
            'evidence':{p.name:sha(p) for p in output.iterdir() if p.is_file() and p.name!='result.json'},
            'scope':'complete locked source observation and native exact readback; executable behavior and playing strength are separate obligations'})
    except Exception as error:
        report.update({'status':'failed','error':str(error),
            'database_mutation_outcome':'consult retained native receipts; submission may have committed' if
                any(c['name'] in ('admission','repeat') for c in report['commands']) else 'admission-not-submitted'})
        save(output/'result.json',report)
        raise
    save(output/'result.json',report)
    return report


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expected-sha',required=True)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--chess-prefix',type=Path,default=Path('/opt/laplace/tools/chess'))
    parser.add_argument('--source-selection',type=Path,help='read-only checkout selection receipt to verify before admission')
    parser.add_argument('--stockfish-source',type=Path,
        default=Path(os.environ['LAPLACE_STOCKFISH_SOURCE'].strip()) if os.environ.get('LAPLACE_STOCKFISH_SOURCE','').strip() else None)
    parser.add_argument('--runtime-root',type=Path)
    parser.add_argument('--timeout',type=float,default=5400)
    args=parser.parse_args()
    try:
        require(args.timeout>0 and args.timeout<=5400,'timeout must be positive and at most5400seconds')
        result=execute(args)
        print(json.dumps({'status':result['status'],'receipt':str(args.output/'result.json')}))
        return 0
    except Exception as error:
        print('Stockfish corpus acceptance failed: '+str(error),file=sys.stderr)
        return 1


if __name__=='__main__':
    raise SystemExit(main())
