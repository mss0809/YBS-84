/*
 * YBS-84の時計文字盤画像送信用コード
 + 送信画像は240x296 RGB565のBMP
 *
 * BLE接続，Write Without Response，Notify待機は ble_helper.cppに分離．
 * このコードBMPを読んで送信パケットを組み立てて送る部分だけを担当．
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <windows.h>

#include "ble_helper.h"

#define ZK_DIAL             0xE4
#define ZK_START             0x51
#define ZK_DATA              0x52
#define ZK_DIAL_INFO         0x53
#define PACKET_HEADER_SIZE   14
#define BLOCK_SIZE           4096
#define BMP_SIZE             142150  /* 70 + 240 * 296 * 2 */

// 通信コマンド用のBig-Endian書込み
void set_be16(uint8_t *buf, uint16_t value) {
    buf[0] = (uint8_t)(value >> 8);
    buf[1] = (uint8_t)value;
}

void set_be32(uint8_t *buf, uint32_t value) {
    buf[0] = (uint8_t)(value >> 24);
    buf[1] = (uint8_t)(value >> 16);
    buf[2] = (uint8_t)(value >> 8);
    buf[3] = (uint8_t)value;
}

uint16_t get_be16(const uint8_t *buf) {
    return (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
}

// データ全体のバイト和．E4 51の最後の2 byteに使う
uint16_t calc_sum16(const uint8_t *data, size_t length) {
    uint32_t sum = 0;
    size_t i;

    for (i = 0; i < length; i++) {
        sum += data[i];
    }
    return (uint16_t)sum;
}

// packet内容を16進数で表示する．
void print_packet(const char *name, const uint8_t *packet, size_t length) {
    size_t i;

    printf("%s (%zu byte): ", name, length);
    for (i = 0; i < length; i++) {
        printf("%02X ", packet[i]);
    }
    printf("\n");
}

// E4 53で時計が要求するpacketサイズを取得する．
int get_dial_info(zk_ble_helper *ble, uint16_t *frame_size) {
    uint8_t query[] = { ZK_DIAL, ZK_DIAL_INFO, 0x01, 0x00 };
    uint8_t reply[64];
    size_t reply_length = 0;

    if (ble_helper_write_without_response(ble, query, sizeof(query)) != 0) {
        return -1;
    }
    if (ble_helper_wait_notification(ble, ZK_DIAL_INFO, reply, sizeof(reply),
                                     &reply_length, 8000) != 0) {
        return -1;
    }
    if (reply_length < 19) {
        return -1;
    }
    if (reply[0] != ZK_DIAL || reply[1] != ZK_DIAL_INFO) {
        return -1;
    }

    *frame_size = get_be16(&reply[11]);
    printf("時計の情報: packet=%u, 表示=%ux%u, 角丸=%u\n",
           *frame_size,
           get_be16(&reply[13]),
           get_be16(&reply[15]),
           get_be16(&reply[17]));
    return 0;
}

// E4 51の開始応答を確認．
int wait_start_ack(zk_ble_helper *ble) {
    uint8_t reply[64];
    size_t reply_length = 0;

    if (ble_helper_wait_notification(ble, ZK_START, reply, sizeof(reply),
                                     &reply_length, 8000) != 0) {
        return -1;
    }
    if (reply_length < 3) {
        return -1;
    }
    if (reply[0] != ZK_DIAL || reply[1] != ZK_START) {
        return -1;
    }
    if (reply[2] > 2) {
        return -1;
    }
    return 0;
}

// 4 KiB blockの送信完了応答 E4 52 を確認
int wait_block_ack(zk_ble_helper *ble) {
    uint8_t reply[64];
    size_t reply_length = 0;

    if (ble_helper_wait_notification(ble, ZK_DATA, reply, sizeof(reply),
                                     &reply_length, 8000) != 0) {
        return -1;
    }
    if (reply_length < 10) {
        return -1;
    }
    if (reply[0] != ZK_DIAL || reply[1] != ZK_DATA) {
        return -1;
    }
    if (reply[9] != 0) {
        return -1;
    }
    return 0;
}

/*
 * 文字盤用BMPを送信する．
 * 240x296 RGB565のBMP
 */
int send_background_image(zk_ble_helper *ble, const uint8_t *bmp_bytes,
                          size_t bmp_length, uint16_t frame_size) {
    uint8_t start_packet[21];
    uint8_t *packet;
    uint16_t data_size;
    uint16_t start_packet_count;
    uint16_t total_packet_count = 0;
    uint16_t packet_number = 0;
    uint16_t running_checksum = 0;
    size_t block_count;
    size_t block;

    if (frame_size <= PACKET_HEADER_SIZE) {
        return -1;
    }
    data_size = frame_size - PACKET_HEADER_SIZE;

    /* E4 51 byte 4-5用．142150 / 223 の切り上げなので638になる． */
    start_packet_count = (uint16_t)(bmp_length / data_size);
    if (bmp_length % data_size != 0) {
        start_packet_count++;
    }

    /* 4 KiB block数を数える． */
    block_count = bmp_length / BLOCK_SIZE;
    if (bmp_length % BLOCK_SIZE != 0) {
        block_count++;
    }

    /* progress用に，実際に送るE4 52の総数を先に数える． */
    for (block = 0; block < block_count; block++) {
        size_t block_start = block * BLOCK_SIZE;
        size_t block_length = bmp_length - block_start;
        uint16_t count;

        if (block_length > BLOCK_SIZE) {
            block_length = BLOCK_SIZE;
        }
        count = (uint16_t)(block_length / data_size);
        if (block_length % data_size != 0) {
            count++;
        }
        total_packet_count += count;
    }

    /* 先頭のE4 51 packetを作る． */
    memset(start_packet, 0, sizeof(start_packet));
    start_packet[0] = ZK_DIAL;
    start_packet[1] = ZK_START;
    start_packet[2] = 0x01;
    start_packet[3] = 0x00;
    set_be16(&start_packet[4], start_packet_count);
    set_be32(&start_packet[6], (uint32_t)bmp_length);
    start_packet[10] = 0x00;
    set_be16(&start_packet[11], data_size);
    start_packet[13] = 0x01;              /* 文字盤タイプ */
    start_packet[14] = 0x01;
    start_packet[15] = 0x05;              /* 時刻文字の位置 */
    start_packet[16] = 0x00;
    set_be16(&start_packet[17], 0xFFFF);  /* 時刻文字の色: 白 */
    set_be16(&start_packet[19], calc_sum16(bmp_bytes, bmp_length));

    printf("開始packetを送信: 全体=%zu byte, E4 51用packet数=%u, 実送信packet数=%u\n",
           bmp_length, start_packet_count, total_packet_count);
    print_packet("E4 51", start_packet, sizeof(start_packet));
    if (ble_helper_write_without_response(ble, start_packet, sizeof(start_packet)) != 0) {
        return -1;
    }
    if (wait_start_ack(ble) != 0) {
        printf("E4 51の応答なし\n");
        return -1;
    }

    /* 時計が転送画面へ切り替わる時間を少し待つ． */
    ble_helper_sleep_ms(ble, 250);

    packet = (uint8_t *)malloc(frame_size);
    if (packet == NULL) {
        return -1;
    }

    /* 4 KiBを一つのblockとして，blockの最後だけACKを待つ． */
    for (block = 0; block < block_count; block++) {
        size_t block_start = block * BLOCK_SIZE;
        size_t block_length = bmp_length - block_start;
        size_t offset;

        if (block_length > BLOCK_SIZE) {
            block_length = BLOCK_SIZE;
        }

        for (offset = 0; offset < block_length; offset += data_size) {
            size_t send_length = block_length - offset;
            size_t i;

            if (send_length > data_size) {
                send_length = data_size;
            }
            packet_number++;

            /* E4 52の14 byteヘッダを作る． */
            packet[0] = ZK_DIAL;
            packet[1] = ZK_DATA;
            packet[2] = 0x01;
            packet[3] = 0x02;
            set_be16(&packet[4], packet_number);
            set_be32(&packet[6], (uint32_t)offset);  /* blockごとに0へ戻る */
            packet[10] = (uint8_t)((packet_number * 100) / total_packet_count);
            packet[11] = 0x00;

            /* BMPの一部を入れ，送信開始からの累積checksumを計算する． */
            memcpy(&packet[PACKET_HEADER_SIZE], &bmp_bytes[block_start + offset], send_length);
            for (i = 0; i < send_length; i++) {
                running_checksum += packet[PACKET_HEADER_SIZE + i];
            }
            set_be16(&packet[12], running_checksum);

            print_packet("E4 52", packet, PACKET_HEADER_SIZE + send_length);

            if (ble_helper_write_without_response(ble, packet,
                                                   PACKET_HEADER_SIZE + send_length) != 0) {
                free(packet);
                return -1;
            }
            ble_helper_sleep_ms(ble, 10);
        }

        printf("block %zu/%zu を送信．ACK待機中...\n", block + 1, block_count);
        if (wait_block_ack(ble) != 0) {
            printf("block %zu のE4 52 ACKなし\n", block + 1);
            free(packet);
            return -1;
        }
    }

    free(packet);
    printf("背景画像の送信とACK確認が完了\n");
    return 0;
}

/* ファイル全体をメモリへ読む． */
int read_binary_file(const char *path, uint8_t **data, size_t *length) {
    FILE *file;
    long file_length;

    *data = NULL;
    *length = 0;
    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }
    fseek(file, 0, SEEK_END);
    file_length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_length <= 0) {
        fclose(file);
        return -1;
    }

    *data = (uint8_t *)malloc((size_t)file_length);
    if (*data == NULL) {
        fclose(file);
        return -1;
    }
    if (fread(*data, 1, (size_t)file_length, file) != (size_t)file_length) {
        free(*data);
        *data = NULL;
        fclose(file);
        return -1;
    }
    fclose(file);
    *length = (size_t)file_length;
    return 0;
}

int main(int argc, char **argv) {
    const char *address = "41:42:E1:92:76:86";
    const char *image_path;
    uint8_t *bmp_bytes;
    size_t bmp_length;
    uint16_t frame_size;
    zk_ble_helper *ble = NULL;

    /* ソースはUTF-8．cmd.exeから起動した場合にも日本語を正しく表示する． */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (argc == 2) {
        image_path = argv[1];
    } else if (argc == 4 && strcmp(argv[1], "--address") == 0) {
        address = argv[2];
        image_path = argv[3];
    } else {
        // 間違った指定が入力されたとき
        return 1;
    }

    if (read_binary_file(image_path, &bmp_bytes, &bmp_length) != 0) {
        printf("画像ファイルの読み込みに失敗: %s\n", image_path);
        return 1;
    }
    if (bmp_length != BMP_SIZE || bmp_bytes[0] != 'B' || bmp_bytes[1] != 'M') {
        printf("対応していないBMPです．\n");
        free(bmp_bytes);
        return 1;
    }

    printf("BLE接続中: %s\n", address);
    if (ble_helper_open(&ble, address) != 0) {
        printf("BLE接続に失敗しました: %s\n", ble_helper_last_error(ble));
        free(bmp_bytes);
        return 1;
    }

    if (get_dial_info(ble, &frame_size) != 0) {
        printf("時計情報を取得に失敗: %s\n", ble_helper_last_error(ble));
        ble_helper_close(ble);
        free(bmp_bytes);
        return 1;
    }
    if (send_background_image(ble, bmp_bytes, bmp_length, frame_size) != 0) {
        printf("背景送信に失敗: %s\n", ble_helper_last_error(ble));
        ble_helper_close(ble);
        free(bmp_bytes);
        return 1;
    }

    ble_helper_close(ble);
    free(bmp_bytes);
    return 0;
}
