#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>

#include "../ble_helper.h"

int main(int argc, char **argv) {
    const char *address = "41:42:E1:92:76:86";
    const char *message_str = "Test Message";
    zk_ble_helper *ble = NULL;
    int i;
    size_t msg_len;
    size_t offset = 0;
    uint8_t index = 0;
    uint8_t packet[32];

    /* ソースはUTF-8で、cmd.exeから起動した場合に日本語を正しく表示するため */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--address") == 0) {
            if (i + 1 < argc) {
                address = argv[i + 1];
                i++;
            }
        } else if (strcmp(argv[i], "--message") == 0) {
            if (i + 1 < argc) {
                message_str = argv[i + 1];
                i++;
            }
        }
    }

    printf("BLE接続中: %s\n", address);
    if (ble_helper_open(&ble, address) != 0) {
        printf("BLE接続に失敗しました: %s\n", ble_helper_last_error(ble));
        return 1;
    }

    msg_len = strlen(message_str);

    while (offset < msg_len) {
        size_t chunk_size = msg_len - offset;
        size_t packet_len;

        if (chunk_size > 17) {
            chunk_size = 17;
        }

        packet[0] = 0x23;
        packet[1] = index;
        packet[2] = 0x01;
        
        memcpy(&packet[3], message_str + offset, chunk_size);
        
        packet_len = 3 + chunk_size;
        
        if (chunk_size < 17) {
            packet[packet_len] = 0xFF;
            packet_len++;
        }
        
        if (ble_helper_write_without_response(ble, packet, packet_len) != 0) {
            printf("送信失敗: %s\n", ble_helper_last_error(ble));
            ble_helper_close(ble);
            return 1;
        }
        
        ble_helper_sleep_ms(ble, 100);
        
        offset += chunk_size;
        index++;
    }
    
    if (msg_len == 0 || (msg_len % 17) == 0) {
        packet[0] = 0x23;
        packet[1] = index;
        packet[2] = 0x01;
        packet[3] = 0xFF;
        if (ble_helper_write_without_response(ble, packet, 4) != 0) {
            printf("終了パケット送信失敗: %s\n", ble_helper_last_error(ble));
        }
        ble_helper_sleep_ms(ble, 100);
    }

    printf("送信完了\n");
    ble_helper_close(ble);
    return 0;
}
