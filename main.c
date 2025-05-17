#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define SECTOR_SIZE 512

void print_hex(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (i % 16 == 0) printf("\n%08lx  ", (unsigned long)i);
        printf("%02X ", data[i]);
    }
    printf("\n");
}

int main() {
    FILE *sd = fopen("\\\\.\\E:", "rb");
    if (!sd) {
        perror("ไม่สามารถเปิด SD Card");
        return 1;
    }

    uint8_t sector[SECTOR_SIZE];

    // อ่าน Sector 0 (MBR หรือ VBR)
    if (fread(sector, 1, SECTOR_SIZE, sd) != SECTOR_SIZE) {
        perror("อ่าน sector ไม่สำเร็จ");
        fclose(sd);
        return 1;
    }

    printf("== ดัมพ์ Sector 0 (MBR หรือ Boot Sector) ==\n");
    print_hex(sector, SECTOR_SIZE);

    // ตรวจสอบ partition type
    uint8_t partition_type = sector[0x1C2];
    uint32_t partition_start = *(uint32_t *)&sector[0x1C6];
    printf("\nPartition type: 0x%02X\n", partition_type);
    printf("Partition starts at sector: %u\n", partition_start);

    // อ่าน Boot Sector ของ partition จริง (ถ้ามี)
    if (partition_start > 0) {
        fseek(sd, partition_start * SECTOR_SIZE, SEEK_SET);
        if (fread(sector, 1, SECTOR_SIZE, sd) != SECTOR_SIZE) {
            perror("อ่าน boot sector ของ partition ไม่สำเร็จ");
            fclose(sd);
            return 1;
        }

        printf("\n== ดัมพ์ Boot Sector ของ Partition (Sector %u) ==\n", partition_start);
        print_hex(sector, SECTOR_SIZE);

        // ข้อมูลน่าสนใจใน boot sector:
        uint16_t bytes_per_sector = *(uint16_t *)&sector[11];
        uint8_t sectors_per_cluster = sector[13];
        uint16_t reserved_sectors = *(uint16_t *)&sector[14];
        uint8_t fat_count = sector[16];
        uint32_t total_sectors = *(uint32_t *)&sector[32];
        uint32_t sectors_per_fat = *(uint32_t *)&sector[36];

        printf("\n--- ข้อมูลจาก Boot Sector ---\n");
        printf("Bytes per sector      : %u\n", bytes_per_sector);
        printf("Sectors per cluster   : %u\n", sectors_per_cluster);
        printf("Reserved sectors      : %u\n", reserved_sectors);
        printf("FAT count             : %u\n", fat_count);
        printf("Total sectors         : %u\n", total_sectors);
        printf("Sectors per FAT       : %u\n", sectors_per_fat);
    }

    fclose(sd);
    return 0;
}
