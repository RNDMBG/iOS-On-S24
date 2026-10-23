/* SPDX-License-Identifier: GPL-2.0-only
 * e1s GICv3 pre-initialization before jumping to XNU.
 *
 * Adapted from ios-topaz shim/shim.c test_gic(), which proved that XNU
 * needs the NS distributor enabled and the boot CPU's redistributor awake
 * before start.s configures the interrupt controller itself.
 */
#include "gic.h"

#define GICD_BASE	0x10200000UL
#define GICR_BASE	0x10240000UL
#define GICR_SIZE	0x140000UL
#define GICR_PE_SIZE	0x20000UL

#define GICD_CTLR	0x0000
#define GICD_CTLR_RWP	(1U << 31)
#define GICD_ENABLE	0x13U

#define GICR_TYPER	0x0008
#define GICR_WAKER	0x0014
#define GICR_IGROUPR0	0x10080
#define GICR_ICENABLER0	0x10180
#define GICR_IPRIORITYR	0x10400

static inline uint32_t rd32(uintptr_t a)
{
	return *(volatile uint32_t *)a;
}

static inline void wr32(uintptr_t a, uint32_t v)
{
	*(volatile uint32_t *)a = v;
}

static inline void wr8(uintptr_t a, uint8_t v)
{
	*(volatile uint8_t *)a = v;
}

static inline uint64_t rd64(uintptr_t a)
{
	return *(volatile uint64_t *)a;
}

#define RSR(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " #r : "=r"(_v)); _v; })
#define WSR(r, v) __asm__ volatile("msr " #r ", %0" : : "r"((uint64_t)(v)))

static uint32_t mpidr_affinity(uint64_t mpidr)
{
	return (uint32_t)(((mpidr >> 32) & 0xff) << 24 |
	                  ((mpidr >> 16) & 0xff) << 16 |
	                  ((mpidr >> 8) & 0xff) << 8 |
	                  (mpidr & 0xff));
}

int gic_prepare(struct gic_info *out)
{
	out->gicd = GICD_BASE;
	out->gicr = 0;
	out->mpidr = RSR(mpidr_el1);

	uint64_t sre = RSR(icc_sre_el1);
	if (!(sre & 1)) {
		WSR(icc_sre_el1, sre | 1);
		__asm__ volatile("isb");
		sre = RSR(icc_sre_el1);
	}
	out->sre = (int)(sre & 1);
	if (!(sre & 1))
		return -1;

	uint32_t ctlr = rd32(GICD_BASE + GICD_CTLR);
	wr32(GICD_BASE + GICD_CTLR, ctlr | GICD_ENABLE);
	for (int i = 0; i < 1000000 && (rd32(GICD_BASE + GICD_CTLR) & GICD_CTLR_RWP); i++)
		;

	uint32_t aff = mpidr_affinity(out->mpidr);
	uintptr_t gicr = 0;
	for (uintptr_t off = 0; off < GICR_SIZE; off += GICR_PE_SIZE) {
		uint64_t typer = rd64(GICR_BASE + off + GICR_TYPER);
		if ((uint32_t)(typer >> 32) == aff) {
			gicr = GICR_BASE + off;
			break;
		}
		if (typer & (1ULL << 4))
			break;
	}
	if (!gicr)
		return -2;
	out->gicr = gicr;

	uint32_t waker = rd32(gicr + GICR_WAKER);
	if (waker & 2) {
		wr32(gicr + GICR_WAKER, waker & ~2U);
		for (int i = 0; i < 1000000 && (rd32(gicr + GICR_WAKER) & 4); i++)
			;
	}

	wr32(gicr + GICR_IGROUPR0, 0xffffffffU);
	wr32(gicr + GICR_ICENABLER0, 0xffffffffU);
	for (int id = 0; id < 32; id++)
		wr8(gicr + GICR_IPRIORITYR + id, 0xa0);

	uint64_t icc_ctlr = RSR(icc_ctlr_el1);
	WSR(icc_ctlr_el1, icc_ctlr & ~2ULL);
	WSR(icc_pmr_el1, 0xf0);
	WSR(icc_bpr1_el1, 0);
	WSR(icc_igrpen1_el1, 1);
	__asm__ volatile("isb");

	return 0;
}
