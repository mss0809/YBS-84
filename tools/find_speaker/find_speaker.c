#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <windows.h>

#include "../ble_helper.h"

int main(int argc, char **argv) {
    const char *address = "41:42:E1:92:76:86";
    zk_ble_helper *ble = NULL;
    uint8_t command[] = { 0xFB, 0x06, 0x01, 0x07, 0x01, 0x0A, 0x01 };

    /* UTF-8の日本語出力設定 */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (argc == 3 && strcmp(argv[1], "--address") == 0) {
        address = argv[2];
    } else if (argc != 1) {
        printf("使い方: %s [--address AA:BB:CC:DD:EE:FF]\n", argv[0]);
        return 1;
    }

    printf("BLE接続中: %s\n", address);
    if (ble_helper_open(&ble, address) != 0) {
        printf("BLE接続に失敗しました: %s\n", ble_helper_last_error(ble));
        return 1;
    }

    if (ble_helper_write_without_response(ble, command, sizeof(command)) != 0) {
        printf("コマンドの送信に失敗しました: %s\n", ble_helper_last_error(ble));
        ble_helper_close(ble);
        return 1;
    }

    printf("スピーカー探索コマンドを送信しました\n");

    ble_helper_close(ble);
    return 0;
}
