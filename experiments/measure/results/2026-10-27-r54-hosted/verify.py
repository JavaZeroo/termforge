"""Verify the immutable first hosted failure archive; do not run measurements."""
import hashlib
import io
import json
from pathlib import Path
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from experiments.measure.r54_contract import validate_sample


def main():
    data = Path(__file__).with_name('evidence.zip').read_bytes()
    assert len(data) == 164608
    assert hashlib.sha256(data).hexdigest() == '9ade502f03419f55295b4e9f4df9dc136c5c4d50ace02d41417fea1de6952917'
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        read = lambda name: json.loads(archive.read(name).decode('utf-8'))
        publication = read('publication.json')
        review = publication['post_publication_review']
        assert review['source_provider_archive_sha256'] == '6a2713dd96d86ce66e2462a142d46b72e915ccc4cfd45bd3b42c5e7486c3cef5'
        assert review['measurement_streams_unchanged'] is True
        assert {item['path'] for item in review['changes']} == {'bundle/baseline-build.stderr', 'bundle/candidate-build.stderr'}
        inventory = {item['path']: item for item in publication['artifacts']}
        assert len(inventory) == len(publication['artifacts']) == 130
        assert len(archive.namelist()) == 131
        assert set(archive.namelist()) == set(inventory) | {'publication.json'}
        exact = 0
        for name, item in inventory.items():
            payload = archive.read(name)
            assert len(payload) == item['published_bytes'], name
            assert hashlib.sha256(payload).hexdigest() == item['published_sha256'], name
            if item['treatment'] == 'byte_exact_owned_synthetic_stream':
                assert item['original_bytes'] == len(payload), name
                assert item['original_sha256'] == item['published_sha256'], name
                exact += 1
        assert exact == 56
        hosted = read('hosted.json')
        assert hosted['status'] == 'failed' and hosted['performance_gate'] == 'not_evaluated'
        assert hosted['source'] == 'f734d29bc4f8a120eac59da3acac3169d49231b2'
        assert hosted['hosted_identity'] == publication['hosted_identity']
        assert hosted['hosted_identity']['GITHUB_RUN_ID'] == '40490861301'
        assert hosted['hosted_identity']['GITHUB_RUN_ATTEMPT'] == '1'
        prefix = 'batch-20261026/'
        manifest = read(prefix + 'manifest.json')
        assert manifest['status'] == 'failed' and manifest['attempted'] == 24 and manifest['valid_samples'] == 23
        plan = read('bundle/plan-v1.json')
        schedule = plan['protocol']['schedules'][0]
        assert schedule['seed'] == 20261026
        records = []
        for sequence in range(24):
            directory = prefix + f'samples/{sequence:03d}/'
            record = read(directory + 'record.json')
            assert record['item'] == schedule['samples'][sequence]
            assert record['status'] == ('valid' if sequence < 23 else 'failed')
            assert record['exit_code'] == 0 and not record['timed_out']
            result = read(directory + 'stdout.bin')
            assert result == record['result']
            validate_sample(result, record['item'])
            for name, identity in record['streams'].items():
                payload = archive.read(directory + name)
                assert len(payload) == identity['bytes'] and hashlib.sha256(payload).hexdigest() == identity['sha256']
            environment = record['environment']
            remainder = environment['busy_seconds'] - result['interval_cpu_seconds']
            upper = remainder * 100 / environment['total_seconds']
            assert abs(upper - environment['unattributed_busy_percent_upper_estimate']) < 1e-12
            assert (0 <= upper <= 5) if sequence < 23 else upper > 5
            records.append(record)
        assert [json.loads(line) for line in archive.read(prefix + 'samples/samples.jsonl').splitlines()] == records
        assert prefix + 'samples/summary.json' not in inventory
        assert not any(name.startswith('batch-20261027/') for name in inventory)
    print(json.dumps(dict(archive_files=131, hashed_evidence_files=130, byte_exact_streams=56,
                          attempted=24, valid_records=23, correct_outputs=24,
                          full_batches=0, performance_gate='not_evaluated')))


if __name__ == '__main__':
    main()
