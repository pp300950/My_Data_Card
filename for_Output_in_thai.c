#include <stdio.h>
#include <windows.h> // สำหรับ SetConsoleOutputCP()

int main() {
    // ตั้งค่า code page ของ console ให้เป็น UTF-8
    SetConsoleOutputCP(65001);

    // ใช้ printf แสดงข้อความภาษาไทย
    printf("สวัสดี\n");

    return 0;
}
