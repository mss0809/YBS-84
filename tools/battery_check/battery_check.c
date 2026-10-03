#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <windows.h>

#include "../ble_helper.h"

int main(int argc, char **argv) {
    const char *address = "41:42:E1:92:76:86";
    zk_ble_helper *ble = NULL;
    uint8_t cmd[1];
    uint8_t prefix[1];
    uint8_t out_buf[64];
    size_t out_len = 0;
    int i;

    /* ソースはUTF-8．cmd.exeから起動した場合にも日本語を正しく表示する． */
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

    cmd[0] = 0x14;
    printf("バッテリー残量照会コマンド(0x14)を送信...\n");
    if (ble_helper_write_without_response(ble, cmd, sizeof(cmd)) != 0) {
        printf("送信に失敗しました: %s\n", ble_helper_last_error(ble));
        ble_helper_close(ble);
        return 1;
    }

    prefix[0] = 0x94;
    printf("応答(プレフィックス0x94)を待機中...\n");
    if (ble_helper_wait_notification_prefix(ble, prefix, sizeof(prefix),
                                            out_buf, sizeof(out_buf), &out_len, 5000) != 0) {
        printf("応答の待機に失敗しました(タイムアウト等): %s\n", ble_helper_last_error(ble));
        ble_helper_close(ble);
        return 1;
    }

    if (out_len >= 2) {
        printf("バッテリー残量: %d%%\n", out_buf[1]);
    } else {
        printf("エラー: 応答データが短すぎます (len=%zu)\n", out_len);
    }

    ble_helper_close(ble);
    return 0;
}
