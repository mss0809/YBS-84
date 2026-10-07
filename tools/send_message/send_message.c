#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#include "../ble_helper.h"

/* アイコン名または1-8の数値を、時計へ送るアイコンIDへ変換する。 */
int parse_icon(const wchar_t *text, uint8_t *icon_type) {
    int number;

    if (wcscmp(text, L"sms") == 0) {
        *icon_type = 1;
        return 0;
    }
    if (wcscmp(text, L"wechat") == 0) {
        *icon_type = 2;
        return 0;
    }
    if (wcscmp(text, L"qq") == 0) {
        *icon_type = 3;
        return 0;
    }
    if (wcscmp(text, L"dingtalk") == 0) {
        *icon_type = 4;
        return 0;
    }
    if (wcscmp(text, L"whatsapp") == 0) {
        *icon_type = 5;
        return 0;
    }
    if (wcscmp(text, L"facebook") == 0) {
        *icon_type = 6;
        return 0;
    }
    if (wcscmp(text, L"twitter") == 0) {
        *icon_type = 7;
        return 0;
    }
    if (wcscmp(text, L"other") == 0) {
        *icon_type = 8;
        return 0;
    }

    number = (int)wcstol(text, NULL, 10);
    if (number >= 1 && number <= 8) {
        *icon_type = (uint8_t)number;
        return 0;
    }
    return -1;
}

const char *icon_name(uint8_t icon_type) {
    switch (icon_type) {
    case 1: return "SMS";
    case 2: return "WeChat";
    case 3: return "QQ";
    case 4: return "DingTalk";
    case 5: return "WhatsApp";
    case 6: return "Facebook";
    case 7: return "Twitter";
    case 8: return "Other";
    default: return "不明";
    }
}

/* UTF-16 のコマンドライン引数を、時計へ送る UTF-8 バイト列に変換する。 */
int wide_to_utf8(const wchar_t *text, char **utf8_text) {
    int byte_count;
    char *converted;

    byte_count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                                     NULL, 0, NULL, NULL);
    if (byte_count == 0) {
        return -1;
    }

    converted = (char *)malloc((size_t)byte_count);
    if (converted == NULL) {
        return -1;
    }

    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                            converted, byte_count, NULL, NULL) == 0) {
        free(converted);
        return -1;
    }

    *utf8_text = converted;
    return 0;
}

/* 時計へ送る packet を確認できるように、常に16進数で表示する。 */
void print_packet(const uint8_t *packet, size_t packet_len) {
    size_t i;

    printf("送信:");
    for (i = 0; i < packet_len; i++) {
        printf(" %02X", packet[i]);
    }
    printf("\n");
}

/*
 * 公式アプリの configDeviceSettings() と同じ通知設定 packet を送る。
 * この packet は通知以外の設定項目も含むため、通知以外は安全側で無効・0にする。
 */
int enable_notification_icons(zk_ble_helper *ble) {
    uint8_t settings[] = {
        0x02, 0x02,
        0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x01, 0x01, 0x03
    };

    printf("通知種別を有効化します（通知以外の同居設定は0へ再設定されます）。\n");
    print_packet(settings, sizeof(settings));
    if (ble_helper_write_without_response(ble, settings, sizeof(settings)) != 0) {
        return -1;
    }
    ble_helper_sleep_ms(ble, 100);
    return 0;
}

int wmain(int argc, wchar_t **argv) {
    const wchar_t *address_wide = L"41:42:E1:92:76:86";
    const wchar_t *message_wide = L"Test Message";
    char *address = NULL;
    char *message_str = NULL;
    uint8_t icon_type = 1;
    zk_ble_helper *ble = NULL;
    int i;
    int enable_icons = 0;
    size_t msg_len;
    size_t offset = 0;
    uint8_t index = 0;
    uint8_t packet[32];

    /* 表示用のコードページを UTF-8 にする。引数は wmain で既に UTF-16 として受け取る。 */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    for (i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--address") == 0) {
            if (i + 1 < argc) {
                address_wide = argv[i + 1];
                i++;
            }
        } else if (wcscmp(argv[i], L"--message") == 0) {
            if (i + 1 < argc) {
                message_wide = argv[i + 1];
                i++;
            }
        } else if (wcscmp(argv[i], L"--icon") == 0) {
            if (i + 1 < argc) {
                if (parse_icon(argv[i + 1], &icon_type) != 0) {
                    printf("不明なアイコンです。\n");
                    printf("sms, wechat, qq, dingtalk, whatsapp, facebook, twitter, other または 1-8 を指定してください。\n");
                    return 1;
                }
                i++;
            }
        } else if (wcscmp(argv[i], L"--enable-notification-icons") == 0) {
            enable_icons = 1;
        }
    }

    if (wide_to_utf8(address_wide, &address) != 0 ||
        wide_to_utf8(message_wide, &message_str) != 0) {
        printf("UTF-8 への文字コード変換に失敗しました。\n");
        free(address);
        free(message_str);
        return 1;
    }

    printf("BLE接続中: %s\n", address);
    printf("通知アイコン: %s (ID=%u)\n", icon_name(icon_type), icon_type);
    if (ble_helper_open(&ble, address) != 0) {
        printf("BLE接続に失敗しました: %s\n", ble_helper_last_error(ble));
        free(address);
        free(message_str);
        return 1;
    }

    if (enable_icons) {
        if (enable_notification_icons(ble) != 0) {
            printf("通知種別の設定に失敗しました: %s\n", ble_helper_last_error(ble));
            ble_helper_close(ble);
            free(address);
            free(message_str);
            return 1;
        }
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
        packet[2] = icon_type;
        
        memcpy(&packet[3], message_str + offset, chunk_size);
        
        packet_len = 3 + chunk_size;
        
        if (chunk_size < 17) {
            packet[packet_len] = 0xFF;
            packet_len++;
        }

        print_packet(packet, packet_len);
        if (ble_helper_write_without_response(ble, packet, packet_len) != 0) {
            printf("送信失敗: %s\n", ble_helper_last_error(ble));
            ble_helper_close(ble);
            free(address);
            free(message_str);
            return 1;
        }
        
        ble_helper_sleep_ms(ble, 100);
        
        offset += chunk_size;
        index++;
    }
    
    if (msg_len == 0 || (msg_len % 17) == 0) {
        packet[0] = 0x23;
        packet[1] = index;
        packet[2] = icon_type;
        packet[3] = 0xFF;
        print_packet(packet, 4);
        if (ble_helper_write_without_response(ble, packet, 4) != 0) {
            printf("終了パケット送信失敗: %s\n", ble_helper_last_error(ble));
        }
        ble_helper_sleep_ms(ble, 100);
    }

    printf("送信完了\n");
    ble_helper_close(ble);
    free(address);
    free(message_str);
    return 0;
}
