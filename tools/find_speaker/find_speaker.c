#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <windows.h>

#include "../ble_helper.h"

/* 送受信した packet を確認できるように、16進数で表示する。 */
void print_packet(const char *label, const uint8_t *packet, size_t packet_len) {
    size_t i;

    printf("%s:", label);
    for (i = 0; i < packet_len; i++) {
        printf(" %02X", packet[i]);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    const char *address = "41:42:E1:92:76:86";
    zk_ble_helper *ble = NULL;
    const uint8_t capability_query[] = { 0x03, 0x00 };
    const uint8_t capability_prefix[] = { 0x83 };
    uint8_t command[] = { 0xFB, 0x06, 0x01, 0x07, 0x01, 0x0A, 0x01 };
    const uint8_t reply_prefix[] = { 0xFB, 0x06 };
    uint8_t capability[64];
    size_t capability_len = 0;
    uint8_t reply[64];
    size_t reply_len = 0;

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

    /* 探索機能の対応 bit は能力応答 83 の byte 18 bit 1 にある。 */
    print_packet("能力照会", capability_query, sizeof(capability_query));
    if (ble_helper_write_without_response(ble, capability_query, sizeof(capability_query)) != 0) {
        printf("能力照会の送信に失敗しました: %s\n", ble_helper_last_error(ble));
    } else if (ble_helper_wait_notification_prefix(ble, capability_prefix, sizeof(capability_prefix),
                                                    capability, sizeof(capability), &capability_len, 2000) == 0) {
        print_packet("能力応答", capability, capability_len);
        if (capability_len > 18 && (capability[18] & 0x02) != 0) {
            printf("探索機能: 対応と申告されています\n");
        } else {
            printf("探索機能: 能力応答では未対応です\n");
        }
    } else {
        printf("83 の能力応答は2秒以内に届きませんでした。\n");
    }

    /* 公式アプリの FreeFitDevice.findHeadphones(1) と同じ開始 packet。 */
    print_packet("送信", command, sizeof(command));
    if (ble_helper_write_without_response(ble, command, sizeof(command)) != 0) {
        printf("コマンドの送信に失敗しました: %s\n", ble_helper_last_error(ble));
        ble_helper_close(ble);
        return 1;
    }

    /* アプリは FB 06 の応答で探索状態を受け取る。応答がなければ非対応の手掛かりになる。 */
    if (ble_helper_wait_notification_prefix(ble, reply_prefix, sizeof(reply_prefix),
                                            reply, sizeof(reply), &reply_len, 2000) == 0) {
        print_packet("応答", reply, reply_len);
    } else {
        printf("FB 06 の応答は2秒以内に届きませんでした。\n");
    }

    ble_helper_close(ble);
    return 0;
}
