/*
 * Obelisk OS - ACPI S5 poweroff path
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <obelisk/bootinfo.h>
#include <obelisk/acpi_power.h>
#include <obelisk/zig_acpi.h>
#include <arch/cpu.h>

#define ACPI_PM1_SLP_TYP_SHIFT 10U
#define ACPI_PM1_SLP_TYP_MASK  (0x7U << ACPI_PM1_SLP_TYP_SHIFT)
#define ACPI_PM1_SLP_EN        (1U << 13)

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __packed;

static bool acpi_table_valid(const struct acpi_sdt_header *h) {
    if (!h || h->length < sizeof(*h)) {
        return false;
    }
    return zig_acpi_checksum_ok(h, h->length) != 0;
}

static uint32_t read_u32(const uint8_t *p) {
    uint32_t v = 0;
    memcpy(&v, p, sizeof(v));
    return v;
}

static uint64_t read_u64(const uint8_t *p) {
    uint64_t v = 0;
    memcpy(&v, p, sizeof(v));
    return v;
}

static const struct acpi_sdt_header *acpi_map_header(uint64_t phys) {
    const struct acpi_sdt_header *h;
    if (phys == 0) {
        return NULL;
    }
    h = (const struct acpi_sdt_header *)PHYS_TO_VIRT(phys);
    if (!acpi_table_valid(h)) {
        return NULL;
    }
    return h;
}

static const struct acpi_sdt_header *acpi_find_fadt(uint64_t sdt_phys, bool xsdt) {
    const struct acpi_sdt_header *sdt = acpi_map_header(sdt_phys);
    const uint8_t *base;
    uint32_t entsz;
    uint32_t count;

    if (!sdt || sdt->length < sizeof(*sdt)) {
        return NULL;
    }
    base = (const uint8_t *)sdt + sizeof(*sdt);
    entsz = xsdt ? 8U : 4U;
    if (sdt->length < sizeof(*sdt) + entsz) {
        return NULL;
    }
    count = (sdt->length - (uint32_t)sizeof(*sdt)) / entsz;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t phys = xsdt ? read_u64(base + i * entsz) : (uint64_t)read_u32(base + i * entsz);
        const struct acpi_sdt_header *h = acpi_map_header(phys);
        if (h && memcmp(h->signature, "FACP", 4) == 0) {
            return h;
        }
    }
    return NULL;
}

static bool aml_parse_pkglen(const uint8_t *p, size_t avail, uint32_t *pkg_len, size_t *consumed) {
    uint8_t lead;
    uint8_t follow;
    uint32_t len;
    if (!p || !pkg_len || !consumed || avail == 0) {
        return false;
    }
    lead = p[0];
    follow = (uint8_t)(lead >> 6);
    if ((size_t)follow + 1U > avail || follow > 3) {
        return false;
    }
    len = (uint32_t)(lead & 0x0F);
    for (uint8_t i = 0; i < follow; i++) {
        len |= ((uint32_t)p[1 + i]) << (4U + 8U * i);
    }
    *pkg_len = len;
    *consumed = (size_t)follow + 1U;
    return true;
}

static bool aml_parse_integer(const uint8_t *p, size_t avail, uint8_t *value, size_t *consumed) {
    if (!p || !value || !consumed || avail == 0) {
        return false;
    }
    if (p[0] == 0x00) {
        *value = 0;
        *consumed = 1;
        return true;
    }
    if (p[0] == 0x01) {
        *value = 1;
        *consumed = 1;
        return true;
    }
    if (p[0] == 0x0A && avail >= 2) {
        *value = p[1];
        *consumed = 2;
        return true;
    }
    if (p[0] == 0x0B && avail >= 3) {
        *value = p[1];
        *consumed = 3;
        return true;
    }
    if (p[0] == 0x0C && avail >= 5) {
        *value = p[1];
        *consumed = 5;
        return true;
    }
    if (p[0] == 0x0E && avail >= 9) {
        *value = p[1];
        *consumed = 9;
        return true;
    }
    return false;
}

static bool acpi_find_s5_types(const uint8_t *aml, size_t len, uint8_t *typa, uint8_t *typb) {
    size_t base = 0;
    int64_t rel;
    if (!aml || !typa || !typb || len < 8) {
        return false;
    }
    while (base + 8 < len) {
        rel = zig_find_s5_name_offset(aml + base, (uint64_t)(len - base));
        if (rel < 0) {
            return false;
        }
        size_t off = base + (size_t)rel;
        size_t pos = off + 4;
        size_t end = pos + 64;
        if (end > len) {
            end = len;
        }
        while (pos < end && aml[pos] != 0x12) {
            pos++;
        }
        if (pos < end && aml[pos] == 0x12) {
            uint32_t pkg_len = 0;
            size_t pkg_len_sz = 0;
            size_t cur;
            size_t remain;
            uint8_t v0 = 0;
            uint8_t v1 = 0;
            size_t c0 = 0;
            size_t c1 = 0;

            if (!aml_parse_pkglen(aml + pos + 1, len - (pos + 1), &pkg_len, &pkg_len_sz)) {
                return false;
            }
            cur = pos + 1 + pkg_len_sz;
            if ((size_t)(len - (pos + 1 + pkg_len_sz)) < (size_t)pkg_len) {
                return false;
            }
            if (cur >= len) {
                return false;
            }
            cur++; /* NumElements */
            if (cur >= len) {
                return false;
            }
            remain = len - cur;
            if (!aml_parse_integer(aml + cur, remain, &v0, &c0)) {
                return false;
            }
            cur += c0;
            if (cur >= len) {
                return false;
            }
            remain = len - cur;
            if (!aml_parse_integer(aml + cur, remain, &v1, &c1)) {
                return false;
            }
            *typa = (uint8_t)(v0 & 0x7);
            *typb = (uint8_t)(v1 & 0x7);
            return true;
        }
        base = off + 4;
    }
    return false;
}

bool acpi_poweroff_s5(void) {
    const uint8_t *rsdp = bootinfo_acpi_rsdp();
    size_t rsdp_len = bootinfo_acpi_rsdp_len();
    uint8_t rev;
    uint64_t root_sdt_phys = 0;
    bool use_xsdt = false;
    const struct acpi_sdt_header *fadt;
    const struct acpi_sdt_header *dsdt;
    const uint8_t *fadtb;
    const uint8_t *aml;
    size_t aml_len;
    uint64_t dsdt_phys = 0;
    uint32_t pm1a = 0;
    uint32_t pm1b = 0;
    uint8_t pm1_len = 0;
    uint8_t slp_typa = 0;
    uint8_t slp_typb = 0;
    uint16_t pm1_ctl;

    if (!rsdp || rsdp_len < 20 || memcmp(rsdp, "RSD PTR ", 8) != 0) {
        return false;
    }
    if (zig_acpi_checksum_ok(rsdp, 20) == 0) {
        return false;
    }

    rev = rsdp[15];
    if (rev >= 2 && rsdp_len >= 36 && zig_acpi_checksum_ok(rsdp, 36) != 0) {
        root_sdt_phys = read_u64(rsdp + 24);
        use_xsdt = (root_sdt_phys != 0);
    }
    if (!use_xsdt) {
        root_sdt_phys = (uint64_t)read_u32(rsdp + 16);
    }
    if (root_sdt_phys == 0) {
        return false;
    }

    fadt = acpi_find_fadt(root_sdt_phys, use_xsdt);
    if (!fadt || fadt->length < 90) {
        return false;
    }

    fadtb = (const uint8_t *)fadt;
    pm1a = read_u32(fadtb + 64);
    pm1b = read_u32(fadtb + 68);
    pm1_len = fadtb[89];
    if (fadt->length >= 148) {
        dsdt_phys = read_u64(fadtb + 140);
    }
    if (dsdt_phys == 0 && fadt->length >= 44) {
        dsdt_phys = (uint64_t)read_u32(fadtb + 40);
    }
    if (pm1a == 0 || pm1_len < 2 || dsdt_phys == 0) {
        return false;
    }

    dsdt = acpi_map_header(dsdt_phys);
    if (!dsdt || memcmp(dsdt->signature, "DSDT", 4) != 0 || dsdt->length <= sizeof(*dsdt)) {
        return false;
    }
    aml = (const uint8_t *)dsdt + sizeof(*dsdt);
    aml_len = dsdt->length - sizeof(*dsdt);
    if (!acpi_find_s5_types(aml, aml_len, &slp_typa, &slp_typb)) {
        return false;
    }

    printk(KERN_INFO "poweroff: ACPI S5 via PM1a=0x%x PM1b=0x%x SLP_TYPa/b=%u/%u\n",
           pm1a, pm1b, slp_typa, slp_typb);

    pm1_ctl = inw((uint16_t)pm1a);
    pm1_ctl = (uint16_t)((pm1_ctl & ~ACPI_PM1_SLP_TYP_MASK) |
                         ((uint16_t)slp_typa << ACPI_PM1_SLP_TYP_SHIFT) |
                         ACPI_PM1_SLP_EN);
    outw((uint16_t)pm1a, pm1_ctl);
    if (pm1b != 0) {
        pm1_ctl = inw((uint16_t)pm1b);
        pm1_ctl = (uint16_t)((pm1_ctl & ~ACPI_PM1_SLP_TYP_MASK) |
                             ((uint16_t)slp_typb << ACPI_PM1_SLP_TYP_SHIFT) |
                             ACPI_PM1_SLP_EN);
        outw((uint16_t)pm1b, pm1_ctl);
    }
    return true;
}
