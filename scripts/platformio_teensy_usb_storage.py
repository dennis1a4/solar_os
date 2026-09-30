"""Adapt the pinned MSC driver for cached RTOS stacks without editing the SDK.

The original library assumes stack buffers live in uncached DTCM. The LCD task
uses OCRAM. Keep command/status DMA buffers in serialized static DTCM storage,
and quarantine a device on timeout until unplugged. Data uses our sector bounce
buffer in usb_storage.cpp. Pattern counts make SDK changes fail explicitly.
"""
from pathlib import Path
import re


def adapt(source):
    source, count = re.subn(
        r'(msCommand(?:Block|Status)Wrapper_t) (\w+) = \(\1\)',
        r'static \1 \2;\n\t\2 = (\1)', source)
    assert count == 10, f'MSC wrapper declarations changed: {count}'
    source = source.replace('#include <Arduino.h>', '''#include <Arduino.h>
extern "C" bool sk_usb_storage_transport_failed();
extern "C" void sk_usb_storage_transport_fault();
static bool solar_msc_wait(volatile bool &done) {
    const uint32_t start = millis();
    while (!done) {
        if (sk_usb_storage_transport_failed() || millis() - start >= 3000) {
            sk_usb_storage_transport_fault();
            return false;
        }
        delay(1);
    }
    return !sk_usb_storage_transport_failed();
}''')
    # The reset method returns void; all other blocking methods return a status.
    start = source.index('void USBDrive::msReset(')
    end = source.index('uint8_t USBDrive::msGetMaxLun(', start)
    prefix, reset, suffix = source[:start], source[start:end], source[end:]
    pattern = r'while\s*\(\s*!(ms\w+Completed)\s*\)\s*yield\(\);'
    reset, count_reset = re.subn(pattern, r'if (!solar_msc_wait(\1)) return;', reset)
    suffix, count_other = re.subn(pattern, r'if (!solar_msc_wait(\1)) return MS_CBW_FAIL;', suffix)
    assert (count_reset, count_other) == (1, 7), (count_reset, count_other)
    source = prefix + reset + suffix
    # A failed/removed device must not retry initialization inside begin().
    old = 'mscInit(); // Do initial init of each instance of a MSC object.'
    assert source.count(old) == 1
    source = source.replace(old, '''if (sk_usb_storage_transport_failed() || mscInit() != MS_CBW_PASS) {
        m_errorCode = MS_CBW_FAIL;
        m_initDone = false;
        return false;
    }''')
    source = source.replace('maxLUN = msGetMaxLun();',
        'maxLUN = msGetMaxLun();\n\tif (sk_usb_storage_transport_failed()) return MS_CBW_FAIL;')
    source = source.replace('msResult = msStartStopUnit(1);',
        'msResult = msStartStopUnit(1);\n\tif (sk_usb_storage_transport_failed()) return MS_CBW_FAIL;')
    # Every I/O entry checks this; no late transfer may overlap a new request.
    source = source.replace('uint8_t USBDrive::checkConnectedInitialized(void) {',
        'uint8_t USBDrive::checkConnectedInitialized(void) {\n\tif (sk_usb_storage_transport_failed()) return MS_CBW_FAIL;')
    for signature, failure in [
        ('void USBDrive::msReset(void) {', 'return;'),
        ('uint8_t USBDrive::msGetMaxLun(void) {', 'return MS_CBW_FAIL;'),
        ('uint8_t USBDrive::msGetCSW(void) {', 'return MS_CBW_FAIL;'),
        ('uint8_t USBDrive::msTestReady() {', 'return MS_CBW_FAIL;'),
        ('uint8_t USBDrive::msStartStopUnit(uint8_t mode) {', 'return MS_CBW_FAIL;'),
    ]:
        assert source.count(signature) == 1, signature
        source = source.replace(signature,
            signature + '\n    if (sk_usb_storage_transport_failed()) ' + failure)
    source = source.replace('uint8_t CSWResult = 0;',
        'if (sk_usb_storage_transport_failed()) return MS_CBW_FAIL;\n\tuint8_t CSWResult = 0;')
    source = source.replace('msResult = msTestReady();',
        'msResult = msTestReady();\n\t\tif (sk_usb_storage_transport_failed()) return MS_CBW_FAIL;')
    return source


if 'Import' in globals():
    Import('env')

    def replace_msc(build_env, node):
        original = Path(node.srcnode().get_abspath())
        target = Path(env.subst('$BUILD_DIR')) / 'usb_storage_driver' / original.name
        target.parent.mkdir(parents=True, exist_ok=True)
        result = adapt(original.read_text())
        if not target.exists() or target.read_text() != result:
            target.write_text(result)
        build_env.AppendUnique(CPPPATH=[str(original.parent)])
        return build_env.File(str(target.resolve()))

    env.AddBuildMiddleware(replace_msc, '*USBHost_t36/MassStorageDriver.cpp')
