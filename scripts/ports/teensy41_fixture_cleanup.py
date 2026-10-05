"""Remove only explicitly supplied, generated test roots after successful tests."""
import re

ROOT = re.compile(r"/(?:sd/|flash/|usb/)?(?:_solaros_flash_[0-9a-f]{10}|_solaros_files_[0-9a-f]{8}|_solaros_pynet_[0-9a-f]{10}|_apps_[0-9a-f]{8}|_usb_test_[0-9a-f]{8})")


def cleanup_fixtures(command, roots, report, keep=False):
    roots = list(roots)
    # Validate the entire batch before the first destructive command.
    if len(set(roots)) != len(roots) or any(not ROOT.fullmatch(p) for p in roots):
        raise ValueError('Cleanup requires distinct, exact generated fixture roots')
    report['fixtures_retained'] = roots.copy()
    report['fixtures_removed'] = []
    if keep:
        return
    for path in roots:
        output = command('rm -r ' + path)
        if 'rm:' in output or 'shell:' in output:
            raise RuntimeError('Fixture cleanup failed: ' + output)
        output = command('ls ' + path)
        if not re.search(r'^ls: cannot open ' + re.escape(path) + r': No such file or directory\s*$', output, re.M):
            raise RuntimeError('Fixture removal not confirmed: ' + output)
        report['fixtures_removed'].append(path)
        report['fixtures_retained'].remove(path)
