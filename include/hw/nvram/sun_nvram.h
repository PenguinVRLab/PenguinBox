#ifndef SUN_NVRAM_H
#define SUN_NVRAM_H

struct Sun_nvram {
    uint8_t type;
    uint8_t machine_id;
    uint8_t macaddr[6];
    uint8_t date[4];
    uint8_t hostid[3];
    uint8_t checksum;
};

static inline void
Sun_init_header(struct Sun_nvram *header, const uint8_t *macaddr, int machine_id)
{
    uint8_t tmp, *tmpptr;
    unsigned int i;

    header->type = 1;
    header->machine_id = machine_id & 0xff;
    memcpy(&header->macaddr, macaddr, 6);
    memcpy(&header->hostid , &macaddr[3], 3);

    tmp = 0;
    tmpptr = (uint8_t *)header;
    for (i = 0; i < 15; i++)
        tmp ^= tmpptr[i];

    header->checksum = tmp;
}
#endif
