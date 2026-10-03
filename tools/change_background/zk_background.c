#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// 16bit / 32bitの数値を配列に書き込む関数

// Big-Endian 上位バイトが先．通信コマンド用．
void set_be16(uint8_t *buf, uint16_t val){
    buf[0] = (val >> 8) & 0xFF;
    buf[1] = val & 0xFF;
}

void set_be32(uint8_t *buf, uint32_t val){
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8)  & 0xFF;
    buf[3] = val & 0xFF;
}

// Little-Endian 送信画像用．下位バイトが先．
void set_le16(uint8_t *buf, uint16_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
}

void set_le32(uint8_t *buf, uint16_t val){
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = (val >> 16) & 0xFF;
    buf[3] = (val >> 24) & 0xFF;
}

// データ全体のバイト和を計算する関数．チェックサム用．
uint16_t calc_sum16(const uint8_t *data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint16_t)(sum & 0xFFFF);
}

// BLE送信，ACK待機，ミリ秒停止用補助関数
int ble_send(const uint8_t *data, size_t len) {
    // BLEのWrite without Response送信関数を呼ぶ

    return 0;
}

int wait_ack(uint8_t cmd){
    // デバイスから指定コマンドの通知(Notifiy)が来るのを待つ関数

    return 0;
}

void sleep_ms(int ms){
    // 環境に合わせてSleep関数を書く．
    Sleep(ms); // for Windows
}

// メインの背景画像送信処理
int send_background_image(const uint8_t *bmp_bytes, size_t bmp_len, uint16_t frame_size){

    // 1パケットに入るデータサイズ（14バイトのヘッダー分を引く）
    uint16_t chunk_size = frame_size - 14;

    // パケット数の計算
    // 4096バイト(4KiB)ごとにブロック分割して総パケット数を計算
    size_t block_count = (bmp_len + 4095) / 4096;
    size_t total_packets = 0;
    
    for (size_t b = 0; b < block_count; b++) {
        // ブロックごとのバイト数を求める
        size_t b_len = 4096;
        if (b == block_count - 1) {
            b_len = bmp_len - (b * 4096); // 最後のブロックの余り
        }
            
        // このブロックで使うパケット数を計算
        size_t packets_in_block = b_len / chunk_size;
        if (b_len % chunk_size != 0) {
            packets_in_block++;
        }
        
        // 全体のパケット数に加算
        total_packets += packets_in_block;
    }

    // 転送パケットを作る
    uint8_t start_pkt[21];
    memset(start_pkt, 0, sizeof(start_pkt)); // 送信用配列を0で初期化

    start_pkt[0] = 0xE4;
    start_pkt[1] = 0x51; // 開始コマンド
    start_pkt[2] = 0x01;
    start_pkt[3] = 0x00;

    set_be16(&start_pkt[4], (uint16_t)total_packets); // 16bitのデータ．総パケット数をBig-Endianに変換
    set_be32(&start_pkt[6], (uint32_t)bmp_len); // 32Byteのデータ．全データサイズ (Big-Endian)に変換
    start_pkt[10] = 0x00;
    set_be16(&start_pkt[11], chunk_size); // 1パケットのデータ長

    start_pkt[13] = 0x01; // dial_type 機種によって異なる
    start_pkt[14] = 0x01;
    start_pkt[15] = 0x05; // 時刻の文字の位置. 0x05 = 中央
    start_pkt[16] = 0x00;
    set_be16(&start_pkt[17], 0xFFFF); // 時計の文字の色．RGB565: 白
    set_be16(&start_pkt[19], calc_sum16(bmp_bytes, bmp_len)); // 全データの合計和

    // 開始パケット送信＆ACK待ち
    if (ble_send(start_pkt, 21) != 0){
        return -1;
    }
    if (wait_ack(0x51) != 0) {
        return -1;
    }

    printf("送信開始パケットを出力：");
    for (int i=0; i < 21){
        printf("%02X",start_pkt[i]); // 送信パケットの中身を表示
    }
    printf("\n"); // 最後は改行しておく

    sleep_ms(250); // デバイスが準備完了するまでの間待機

    // 4KiBブロックごとにまとめて画像を送信
    uint8_t *packet = (uint8_t *)malloc(frame_size); // 1パケット用バッファ
    uint16_t packet_num = 0; // 通算パケット番号
    uint16_t running_checksum = 0; // これまでの累計チェックサム

    for (size_t b = 0; b < block_count; b++){
        size_t block_start = b * 4096;

        // 基本は 1 ブロック当たり 4096 バイト
        size_t block_len = 4096;
        // もし「最後のブロック」なら、全サイズから開始位置を引いた「あまりのバイト数」にする
        if (b == block_count - 1) {
            block_len = bmp_len - block_start;
        }

        // 4KiBブロック内での送信ループ
        for (size_t offset = 0; offset < block_len; offset += chunk_size) {
            packet_num++; // パケット番号をインクリメント

            size_t send_len = block_len - offset;
            if (send_len > chunk_size) {
                send_len = chunk_size;
            }

            // 送信バッファを0で初期化
            memset(packet, 0, frame_size);

            // 1. ヘッダー14Byteをセット
            packet[0] = 0xE4;
            packet[1] = 0x52;
            packet[2] = 0x01;
            packet[3] = 0x02;
            set_be16(&packet[4], packet_num); // 通算パケット番号． Big-Endian
            set_be32(&packet[6], (uint32_t)offset);        // ブロック内オフセット．Big-Endian
            packet[10] = (uint8_t)((packet_num * 100) / total_packets); // 進捗率%．整数値
            packet[11] = 0x00;

            // 画像データ部分をコピー＆累計チェックサム加算
            for (size_t i = 0; i< send_len; i++){
                uint8_t val = bmp_bytes[block_start + offset + i];
                packet[14 + i] = val; // ヘッダーの直後にデータを配置
                running_checksum += val; // チェックサムに加算
            }

            // 累計チェックサムの値をヘッダーの12, 13Byte目に書き込む
            set_be16(&packet[12], running_checksum);

            // ACKを待たずにどんどん送信
            ble_send(packet, 14 + send_len);
            sleep_ms(10); // パケット間にdelayを挟む
        }

        // 4KiB分送り終えたら1回だけACK (E4 52) を待つ．
        printf("ブロック %zu/%zu 送信完了．ACK待機中...\n", b + 1, block_count);
        if (wait_ack(0x52) != 0) { // ACK (E4 52) を待つ．
            printf("エラー: ブロック %zu のACKが来ませんでした\n", b + 1);
            free(packet);
            return -1;
        }
    }
    free(packet);
    printf("画像の送信・ACK確認が完了\n");
    return 0;
}