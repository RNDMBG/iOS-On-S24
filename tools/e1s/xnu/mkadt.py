#!/usr/bin/env python3
"""Build the experimental e1s ADT for the SWEET-based XNU scaffold.

Only what the early boot path reads is included. Unlike the SWEET donor, the
e1s RAM window, GICR extent and framebuffer geometry are still assumptions.
Usage: mkadt.py OUT.adt [--dump] [--qemu] [--highmem]
"""
import hashlib, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt  # noqa: E402

PHYS_BASE = 0xA0000000
MEM_SIZE = 0x20000000
HIGH_PHYS_BASE = PHYS_BASE
HIGH_MEM_SIZE = MEM_SIZE
SOC_BASE = 0x10000000

GICD, GICD_SIZE = 0x10200000, 0x10000
GICR, GICR_SIZE = 0x10240000, 0x140000
FB, FB_SIZE = 0xFA200000, 0x009a4000
UFS, UFS_SIZE = 0x17100000, 0x3000
SWEET_ECID = 0x4531533234000001  # 'E1S2' + 4, synthetic (not the phone ECID)
FB_W, FB_H, FB_STRIDE = 1080, 2340, 1080 * 4
TIMEBASE = 25600000
VTIMER_INTID = 27  # virtual timer PPI 11 (16 + 11)
QEMU_GICD, QEMU_GICR, QEMU_TIMEBASE = 0x08000000, 0x080a0000, 62500000
QEMU_VTIMER_INTID = 27

BOOT_MANIFEST_HASH = hashlib.sha384(b'sweet boot manifest').digest()
NVRAM_BANK_SIZE = 0x2000
PANIC_LOG_BASE = PHYS_BASE + MEM_SIZE - 0x100000
PANIC_LOG_SIZE = 0x80000


def padded32(text):
    raw = text.encode()
    if len(raw) >= 32:
        raise ValueError(f'{text!r} does not fit in 32 bytes')
    return raw + b'\0' * (32 - len(raw))


def nvram_chrp_header(sig, size, name):
    """One CHRP partition header, as iokit/Kernel/IONVRAMCHRPHandler.cpp reads it."""
    h = bytearray(16)
    h[0] = sig
    blocks = size // 16
    h[2] = blocks & 0xff
    h[3] = (blocks >> 8) & 0xff
    raw = name.encode()
    h[4:4 + len(raw)] = raw
    checksum = h[0]
    for i in range(2, 16):
        checksum += h[i]
    while checksum > 0xff:
        checksum = (checksum & 0xff) + (checksum >> 8)
    h[1] = checksum
    return bytes(h)


def build_nvram_image():
    """An empty but valid CHRP NVRAM bank (IOKit refuses to start without one)."""
    image = bytearray(NVRAM_BANK_SIZE)
    image[0:16] = nvram_chrp_header(0x5a, 0x20, 'nvram')
    image[0x20:0x30] = nvram_chrp_header(0x70, NVRAM_BANK_SIZE - 0x20, 'common')
    image[20:24] = struct.pack('<I', 1)  # generation
    lo, hi = 1, 0
    for value in image[20:]:
        lo = (lo + value) % 65521
        hi = (hi + lo) % 65521
    image[16:20] = struct.pack('<I', (hi << 16) | lo)
    return bytes(image)


NVRAM_IMAGE = build_nvram_image()

D22AP_ADT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../research/ipsw-x/d22ap.adt')


def d22ap_product(path=D22AP_ADT):
    """/product properties of the real D22AP tree, without the syscfg
    placeholders iBoot would fill in (flag bit set) and the node bookkeeping."""
    if not os.path.exists(path):
        print(f'warning: {path} missing, no /product node', file=sys.stderr)
        return None
    root, _ = adt.parse(open(path, 'rb').read())
    for c in root.children:
        if dict((k, v) for k, f, v in c.props).get('name') == b'product\0':
            return [(k, v) for k, f, v in c.props if not f and k not in ('name', 'AAPL,phandle')]
    raise SystemExit(f'{path}: no /product node')

u32 = lambda *v: struct.pack('<%dI' % len(v), *v)
u64 = lambda *v: struct.pack('<%dQ' % len(v), *v)
cstr = lambda s: s.encode() + b'\0'


def node(name, props=(), children=()):
    n = adt.Node()
    n.props = [('name', 0, cstr(name))] + [(k, 0, v) for k, v in props]
    n.children = list(children)
    return n


def build(seed=None, qemu=False, highmem=False, smp=False):
    """qemu=True: same tree, but with QEMU virt's GIC addresses and CNTFRQ (tools/qemu-sweet.py).

    smp=True publishes all ten e1s CPUs with the DTS MPIDR map. The default is
    still one CPU: the SMP path needs a BOOT command line with cpus=10.
    """
    phys_base, mem_size = (HIGH_PHYS_BASE, HIGH_MEM_SIZE) if highmem else (PHYS_BASE, MEM_SIZE)
    gicd, gicr, timebase = (QEMU_GICD, QEMU_GICR, QEMU_TIMEBASE) if qemu else (GICD, GICR, TIMEBASE)
    seed = seed if seed is not None else os.urandom(64)
    phandle = iter(range(1, 1000))
    ph = lambda: ('AAPL,phandle', u32(next(phandle)))

    chosen = node('chosen', [
        ph(),
        ('dram-base', u64(phys_base)),
        ('dram-size', u64(mem_size)),
        ('random-seed', seed),
        ('debug-enabled', u32(1)),
        ('psci-method', cstr('smc')),
        ('display-rotation', u32(0)),
        ('firmware-version', cstr('iBoot-8422.100.650')),
        ('system-firmware-version', cstr('iBoot-8422.100.650')),
        ('mac-address-wifi0', bytes([0x02, 0x53, 0x32, 0x34, 0x00, 0x01])),
        ('mac-address-bluetooth0', bytes([0x02, 0x53, 0x32, 0x34, 0x00, 0x02])),
        ('software-behavior', b'\0' * 16),
        ('secure-boot', u32(1)),
        ('production-cert', u32(1)),
        ('development-cert', u32(0)),
        ('embedded-panic-log-size', u32(PANIC_LOG_SIZE)),
        ('nvram-bank-size', u32(NVRAM_BANK_SIZE)),
        ('nvram-bank-count', u32(1)),
        ('nvram-current-bank', u32(0)),
        ('nvram-proxy-data', NVRAM_IMAGE),
        ('boot-manifest-hash', BOOT_MANIFEST_HASH),
        ('crypto-hash-method', cstr('sha2-384')),
        ('display-scale', u32(3)),
        ('unique-chip-id', struct.pack('<Q', SWEET_ECID)),
        ('chip-id', u32(0x8015)),
        ('board-id', u32(0x06)),
        ('certificate-production-status', u32(0)),
        ('effective-production-status-ap', u32(0)),
        ('certificate-security-mode', u32(1)),
        ('effective-security-mode-sep', u32(1)),
    ], [node('memory-map', [ph()]),
        node('asmb', [ph(), ('lp-sip0', u64(0x6f)), ('lp-sip1', u32(1))])])

    if smp:
        cpu_table = [
            (0x000, 0, 0, True), (0x100, 0, 1, True),
            (0x200, 0, 2, True), (0x300, 0, 3, True),
            (0x400, 1, 0, False), (0x500, 1, 1, False),
            (0x600, 1, 2, False), (0x700, 2, 0, False),
            (0x800, 2, 1, False), (0x900, 3, 0, False),
        ]
    else:
        cpu_table = [(0x000, 0, 0, False)]

    cpu_nodes = []
    for index, (mpidr, cluster, core, small) in enumerate(cpu_table):
        props = [
            ph(),
            ('device_type', cstr('cpu')),
            ('compatible', cstr('samsung,exynos2400') + cstr('arm,armv8')),
            ('reg', u32(mpidr)),
            ('cpu-id', u32(index)),
            ('cluster-id', u32(cluster)),
            ('cluster-type', u32(0)),
            ('die-cluster-id', u32(cluster)),
            ('cluster-core-id', u32(core)),
            ('l2-cache-id', u32(cluster)),
            ('l3-cache-id', u32(0)),
            ('state', cstr('running')),
        ]
        if small:
            props.append(('small-core', u32(1)))
        props.extend([
            ('timebase-frequency', u32(timebase)),
            ('clock-frequency', u32(2400000000)),
            ('bus-frequency', u32(100000000)),
            ('memory-frequency', u32(3200000000)),
            ('peripheral-frequency', u32(timebase)),
            ('fixed-frequency', u32(timebase)),
        ])
        cpu_nodes.append(node(f'cpu{index}', props))
    cpus = node('cpus', [ph(), ('#address-cells', u32(1)),
                         ('#size-cells', u32(0))], cpu_nodes)

    gic = node('gic', [
        ph(),
        ('device_type', cstr('interrupt-controller')),
        ('compatible', cstr('arm,gic-v3')),
        ('interrupt-controller', b''),
        ('#interrupt-cells', u32(3)),
        ('virtual-timer-intid', u32(QEMU_VTIMER_INTID if qemu else VTIMER_INTID)),
        ('reg', u64(gicd - SOC_BASE, GICD_SIZE, gicr - SOC_BASE, GICR_SIZE)),
    ])
    arm_io = node('arm-io', [
        ph(),
        ('device_type', cstr('e1s-io')),
        ('compatible', cstr('samsung,e1s-io') + cstr('arm-io')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
        ('ranges', u64(0, SOC_BASE, 0x100000000 - SOC_BASE)),
    ], [gic,
        node('wdt', [ph(), ('no-pmu', u32(1))]),
        node('disp0', [
            ph(),
            ('device_type', cstr('display-subsystem')),
            ('compatible', cstr('sweet,display')),
            ('canvas-width', u32(FB_W)),
            ('canvas-height', u32(FB_H)),
        ])])

    fbcon = node('sweet-fbcon', [
        ph(),
        ('reg', u64(FB, FB_SIZE)),
        ('width', u32(FB_W)),
        ('height', u32(FB_H)),
        ('stride', u32(FB_STRIDE)),
    ])

    ufs = node('sweet-ufs', [
        ph(),
        ('compatible', cstr('samsung,ufshci') + cstr('sweet,ufs')),
        ('reg', u64(UFS, UFS_SIZE)),
    ])

    buttons = node('buttons', [
        ph(),
        ('button-names', cstr('volup') + cstr('voldown') + cstr('hold')),
        ('home-button-type', u32(2)),
        ('opposed-power-vol-buttons', u32(1)),
    ])

    product_props = d22ap_product()
    if product_props:
        product_props = [(k, cstr('sRGB') if k == 'artwork-display-gamut' else v)
                         for k, v in product_props]
    product = [node('product', [ph()] + product_props)] if product_props else []

    options = node('options', [ph()])
    pram = node('pram', [ph(), ('reg', u64(PANIC_LOG_BASE, PANIC_LOG_SIZE))])
    kernel_defaults = node('defaults', [ph(), ('kern.vm_compressor', u32(2)),
                                        ('no-effaceable-storage', u32(1))])

    return node('device-tree', [
        ph(),
        ('compatible', cstr('D22AP') + cstr('AppleARM') + cstr('iPhone10,3')),
        ('model', cstr('iPhone10,3')),
        ('target-type', cstr('D22')),
        ('target-sub-type', cstr('D22AP')),
        ('manufacturer', cstr('Apple Inc.')),
        ('device_type', cstr('bootrom')),
        ('platform-name', cstr('t8015')),
        ('serial-number', padded32('E1S00XNU0000001')),
        ('mlb-serial-number', padded32('E1S00XNU0000001MLB')),
        ('model-number', padded32('MQAC2')),
        ('region-info', padded32('LL/A')),
        ('regulatory-model-number', padded32('A1865')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
    ], [chosen, kernel_defaults, cpus, arm_io, fbcon, buttons, options, pram] + product + ([] if qemu else [ufs]))


if __name__ == '__main__':
    paths = [arg for arg in sys.argv[1:] if not arg.startswith('--')]
    if not paths:
        sys.exit('usage: mkadt.py OUTPUT [--qemu] [--highmem] [--smp] [--dump]')
    root = build(qemu='--qemu' in sys.argv, highmem='--highmem' in sys.argv,
                 smp='--smp' in sys.argv)
    blob = adt.serialize(root)
    open(paths[0], 'wb').write(blob)
    if '--dump' in sys.argv:
        adt.dump(adt.parse(blob)[0])
    print(f'{paths[0]}: {len(blob)} bytes', file=sys.stderr)
