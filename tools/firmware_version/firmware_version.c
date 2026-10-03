#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <windows.h>

#include "../ble_helper.h"

int main(int argc, char **argv) {
    const char *address = "41:42:E1:92:76:86";
    zk_ble_helper *ble = NULL;
    int i;
    uint8_t cmd[1];
    uint8_t prefix[1];
    uint8_t reply[256];
    size_t reply_length = 0;

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--address") == 0) {
            if (i + 1 < argc) {
                address = argv[i + 1];
                i++;
            }
        }
    }

    printf("BLE接続中: %s\n", address);
    if (ble_helper_open(&ble, address) != 0) {
        printf("BLE接続に失敗しました: %s\n", ble_helper_last_error(ble));
        return 1;
    }

    cmd[0] = 0x1F;
    if (ble_helper_write_without_response(ble, cmd, sizeof(cmd)) != 0) {
        printf("コマンドの送信に失敗しました: %s\n", ble_helper_last_error(ble));
        ble_helper_close(ble);
        return 1;
    }

    prefix[0] = 0x9F;
    if (ble_helper_wait_notification_prefix(ble, prefix, sizeof(prefix), reply, sizeof(reply), &reply_length, 8000) != 0) {
        printf("応答の待機に失敗しました: %s\n", ble_helper_last_error(ble));
        ble_helper_close(ble);
        return 1;
    }

    printf("受信バイト数: %zu\n", reply_length);

    if (reply_length > 13) {
        printf("Firmware Version: %d.%d\n", reply[12], reply[13]);
    } else {
        printf("受信データが短すぎます\n");
    }

    ble_helper_close(ble);
    return 0;
}
