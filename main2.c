#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h> // สำหรับ SetConsoleOutputCP
#include <direct.h>  // สำหรับ _mkdir

FILE *sd_card_file;
// ขนาดคลัสเตอร์
// unsigned คือไม่มีเครื่องหมายลบ
unsigned int sector_size = 512;
unsigned int sectors_per_cluster = 32;
unsigned int cluster_size;
// เซกเตอร์เเรกที่เริ่ใช้เก็บข้อมูล
unsigned int root_directory_cluster = 2;
unsigned int fat_start_sector = 2;
unsigned int total_fat_sectors = 15997;
unsigned int data_start_sector;

// อ่านข้อมูล 1 sector จากไฟล์ แทน SD card หรือ disk image และเก็บข้อมูลนั้นลงใน buffer ที่เรากำหนด
/*
ตัวอย่างการเรียกฟังชั่น
read_sector(0, buffer); // อ่าน sector แรก → byte 0
read_sector(1, buffer); // อ่าน sector ที่สอง → byte 512
read_sector(2, buffer); // อ่าน sector ที่สาม → byte 1024

*/
void read_sector(unsigned int sector_number, unsigned char *buffer)
{
    // fseek() เลื่อนตำแหน่งของตัวอ่านไฟล์ไปยังตำแหน่งที่ต้องการ
    
    //sector_number * sector_size คือ byte offset ที่ต้องการอ่าน

    //ถ้า sector_number = 0 → offset = 0 (อ่าน sector แรก คือตั้งเเต่ไบต์ที่ 0)
    //ถ้า sector_number = 1 → offset = 512 (อ่าน sector ที่ 2 ถ้า sector_size = 512)

    // SEEK_SET บอกให้เริ่มนับจากจุดเริ่มต้นของไฟล์
    fseek(sd_card_file, sector_number * sector_size, SEEK_SET);

    fread(buffer, 1, sector_size, sd_card_file);
    //อ่านข้อมูลจากไฟล์ sd_card_file จำนวน: sector_size ไบต์ เช่น 512 ไบต์ เเล้วเก็บไว้ใน buffer
}

void read_cluster(unsigned int cluster_number, unsigned char *buffer)
{
    unsigned int start_sector = data_start_sector + (cluster_number - 2) * sectors_per_cluster;
    fseek(sd_card_file, start_sector * sector_size, SEEK_SET);
    fread(buffer, 1, cluster_size, sd_card_file);
}

unsigned int read_fat_entry(unsigned int cluster_number)
{
    unsigned int fat_sector_number = fat_start_sector + (cluster_number * 4) / sector_size;
    unsigned int offset_within_sector = (cluster_number * 4) % sector_size;
    unsigned char sector_buffer[512];

    read_sector(fat_sector_number, sector_buffer);

    unsigned int *entry_ptr = (unsigned int *)&sector_buffer[offset_within_sector];
    return *entry_ptr & 0x0FFFFFFF; // ดึงเฉพาะ 28 บิตล่าง
}

void dump_file_to_disk(const char *filename, unsigned int start_cluster, unsigned int file_size)
{
      char full_path[256];
    snprintf(full_path, sizeof(full_path), "Myfile/%s", filename); // เตรียม path แบบเต็ม

    FILE *output_file = fopen(full_path, "wb");
    if (output_file == NULL)
    {
        perror("ไม่สามารถสร้างไฟล์ได้");
        return;
    }

    unsigned int current_cluster = start_cluster;
    unsigned int remaining_bytes = file_size;
    unsigned char *cluster_buffer = (unsigned char *)malloc(cluster_size);

    while (current_cluster < 0x0FFFFFF8)
    {
        read_cluster(current_cluster, cluster_buffer);

        unsigned int bytes_to_write = (remaining_bytes < cluster_size) ? remaining_bytes : cluster_size;
        fwrite(cluster_buffer, 1, bytes_to_write, output_file);

        remaining_bytes -= bytes_to_write;
        if (remaining_bytes == 0)
        {
            break;
        }

        current_cluster = read_fat_entry(current_cluster);
    }

    free(cluster_buffer);
    fclose(output_file);
    printf("เขียนไฟล์เสร็จสิ้น: %s (%u ไบต์)\n", full_path, file_size);
}

void dump_all_root_directory_files()
{
    unsigned char *directory_buffer = (unsigned char *)malloc(cluster_size);
    read_cluster(root_directory_cluster, directory_buffer);

    printf("กำลังสแกนไดเรกทอรีรากที่คลัสเตอร์ %u...\n", root_directory_cluster);

    int found = 0;

    for (int i = 0; i < cluster_size; i += 32)
    {
        printf("รายการ %d: ไบต์แรก = 0x%02X\n", i / 32, directory_buffer[i]);

        if (directory_buffer[i] == 0x00)
        {
            printf("ไม่มีรายการเพิ่มเติมแล้ว\n");
            break;
        }

        if (directory_buffer[i] == 0xE5 || directory_buffer[i + 11] == 0x0F)
        {
            printf("ข้ามรายการที่ถูกลบหรือเป็นชื่อยาว (LFN)\n");
            continue;
        }

        found = 1;

        char filename[13] = {0};
        snprintf(filename, 13, "%.8s.%.3s", &directory_buffer[i], &directory_buffer[i + 8]);
        for (int j = 0; j < 12; j++)
        {
            if (filename[j] == ' ')
                filename[j] = '\0';
        }

        unsigned short high_word = *(unsigned short *)&directory_buffer[i + 20];
        unsigned short low_word = *(unsigned short *)&directory_buffer[i + 26];
        unsigned int start_cluster = (high_word << 16) | low_word;
        unsigned int file_size = *(unsigned int *)&directory_buffer[i + 28];

        printf("พบไฟล์: %s (%u ไบต์) เริ่มที่คลัสเตอร์ %u\n", filename, file_size, start_cluster);

        dump_file_to_disk(filename, start_cluster, file_size);
    }

    if (!found)
    {
        printf("ไม่พบไฟล์ที่ถูกต้องในไดเรกทอรีราก\n");
    }

    free(directory_buffer);
}

void read_bpb() //ใช้อ่าน BPB (sector 0) เพื่อดั๊มข้อมูลทุกอย่างที่มันเป็นค่ากำหนดออกมา
{
    unsigned char buffer[512];/*สร้าง อาเรย์ของไบต์ขนาด 512 ไบต์ ใช้เก็บข้อมูลที่อ่านมาจาก sector แรกของSD Card*/
    read_sector(0, buffer); 
    //เรียกฟังก์ชัน read_sector() เพื่ออ่านข้อมูลจาก sector หมายเลข 0
    //ข้อมูลที่อ่านได้จะถูกเก็บลงใน buffer ที่สร้างไว้ข้างต้น

    sector_size = *(unsigned short *)&buffer[11];
    sectors_per_cluster = buffer[13];
    unsigned short reserved_sectors = *(unsigned short *)&buffer[14];
    unsigned char number_of_fats = buffer[16];
    total_fat_sectors = *(unsigned int *)&buffer[36];
    root_directory_cluster = *(unsigned int *)&buffer[44];

    fat_start_sector = reserved_sectors;
    data_start_sector = fat_start_sector + number_of_fats * total_fat_sectors;
    cluster_size = sector_size * sectors_per_cluster;

    printf("=== ข้อมูล BPB ===\n");
    printf("ไบต์ต่อเซกเตอร์: %u\n", sector_size);
    printf("เซกเตอร์ต่อคลัสเตอร์: %u\n", sectors_per_cluster);
    printf("เซกเตอร์ที่จองไว้: %u\n", reserved_sectors);
    printf("จำนวน FATs: %u\n", number_of_fats);
    printf("จำนวนเซกเตอร์ FAT ทั้งหมด: %u\n", total_fat_sectors);
    printf("คลัสเตอร์ของไดเรกทอรีราก: %u\n", root_directory_cluster);
    printf("เซกเตอร์เริ่มต้นของ FAT: %u\n", fat_start_sector);
    printf("เซกเตอร์เริ่มต้นของข้อมูล: %u\n", data_start_sector);
    printf("ขนาดของคลัสเตอร์ (ไบต์): %u\n", cluster_size);
    printf("====================\n");
}

int main()
{
    // UTF-8 เพื่อแสดงภาษาไทยในคอนโซล
    SetConsoleOutputCP(65001);
    // สร้างโฟลเดอร์ Myfile หากยังไม่มี
    _mkdir("Myfile");

    printf("เปิด SD card...\n");

    cluster_size = sector_size * sectors_per_cluster;
    /*
    sector_size คือจำนวนไบต์ต่อ 1 เซกเตอร์ ในที่นี้คือ 512
    sectors_per_cluster คือจำนวนเซกเตอร์ที่ประกอบเป็น 1 คลัสเตอร์ ในที่นี้คือ 32
    ถ้า sector_size = 512 และ sectors_per_cluster = 32
    ดังนั้น
    ## cluster_size = 512 * 32 = 16384 ไบต์ หรือ 16 KB ต่อคลัสเตอร์

    */

    data_start_sector = 2 + total_fat_sectors * 2;
    /*
    คำนวณว่า Data Region หรือ บริเวณข้อมูลของไฟล์ เริ่มต้นที่เซกเตอร์หมายเลขใด
    2 คือค่าที่ฮาร์ดโค้ดไว้ว่าเริ่มหลังจากเซกเตอร์ที่จองไว้ เช่น Boot sector, FS Info sector
    total_fat_sectors คือขนาดของ FAT ตารางเดียว ในหน่วยเซกเตอร์ ในที่นี้คือ 15997
    * 2 เพราะในระบบ FAT32 มักมี สอง FAT tables

    ดังนั้น data_start_sector คือเซกเตอร์แรกที่เริ่มเก็บข้อมูลไฟล์จริง (คลัสเตอร์ 2 เป็นต้นไป) */

    sd_card_file = fopen("\\\\.\\E:", "rb");
    if (sd_card_file == NULL)
    {
        perror("ไม่สามารถเปิด SD Card ได้");
        return 1;
    }
    else
    {
        printf("เปิด SD card สำเร็จแล้ว\n");
    }

    read_bpb(); //อ่าน BPB (sector 0) เพื่อกำหนดค่าที่จำเป็น
    /*
    อ่าน ข้อมูลโครงสร้างระบบไฟล์ จาก SD Card
    เช่น ขนาดเซกเตอร์, จำนวน FAT, คลัสเตอร์เริ่มต้นของไดเรกทอรีราก เพื่อใช้ในการวิเคราะห์และเข้าถึงข้อมูลในระบบไฟล์ FAT32
    */
    dump_all_root_directory_files();
    /*
    สแกนและดึงไฟล์ทั้งหมดจาก Root Directory ของ SD Card แล้ว บันทึกไฟล์เหล่านั้นลงดิสก์ในเครื่อง
    เหมือนการ กู้ไฟล์ หรือ ก็อปปี้ไฟล์ทั้งหมดจาก SD ที่อยู่ใน root มาเก็บบนเครื่องคอม  */

    fclose(sd_card_file); // ปิดไฟล์ SD Card หลังจากอ่านข้อมูลเสร็จ
    return 0;
}
