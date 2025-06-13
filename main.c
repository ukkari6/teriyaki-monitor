#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>   // open(), O_RDWR など
#include <unistd.h>  // read(), write(), close()
#include <termios.h> // termios構造体, tcgetattr(), tcsetattr()など
#include <string.h>  // memset()
#include <errno.h>   // errno
#include <ncurses.h> // ncursesライブラリ
#include <ctype.h>   // isprint()
#include <stdlib.h>  // exit(), atexit()

WINDOW *ascii_win = NULL;
WINDOW *hex_win = NULL;
int serial_fd = -1;

void cleanup_resources() {
    if (serial_fd != -1) {
        close(serial_fd);
        serial_fd = -1;
    }
    if (hex_win) delwin(hex_win);
    if (ascii_win) delwin(ascii_win);
    if (isendwin() == FALSE) { // endwinがまだ呼ばれていなければ呼ぶ
        endwin();
    }
}

int main(void){
    const char *portname = "/dev/ttyUSB0";
    struct termios tty;
    char buf[256];
    int n;

    // シリアルポートを開く
    // O_RDWR: 読み書きモード
    // O_NOCTTY: このポートを制御端末にしない
    // O_SYNC: 書き込みが完了するまで待つ (同期I/O)
    serial_fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
    if (serial_fd < 0) {
        perror("Error opening serial port");
        return 1;
    }

    atexit(cleanup_resources); // プログラム終了時にリソースをクリーンアップ

    // 現在のシリアルポート設定を取得
    if (tcgetattr(serial_fd, &tty) != 0) {
        perror("Error from tcgetattr");
        // atexit で close されるのでここでは不要
        return 1;
    }

    // ボーレートを設定 (例: 9600bps)
    // cfsetospeed(&tty, B9600);
    // cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);
    
    // シリアルポートのパラメータを設定 (8N1: 8データビット, パリティなし, 1ストップビット)
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
    tty.c_iflag &= ~IGNBRK;                         // BREAK信号を無視
    tty.c_lflag = 0;                                // ローカルフラグをクリア (エコーなし、カノニカルモードオフなど)
    tty.c_oflag = 0;                                // 出力フラグをクリア (出力処理なし)
    
    // 読み取りタイムアウト設定 (VMIN=0, VTIME=5 -> 0.5秒タイムアウト)
    // VMIN = 0: 読み取るべき最小文字数を0に設定 (非ブロッキングに近いがVTIMEで制御)
    // VTIME = 5: 0.1秒単位のタイムアウト時間 (5 = 0.5秒)
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 5;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);          // XON/XOFFフロー制御を無効化

    tty.c_cflag |= (CLOCAL | CREAD);                // ローカル接続、読み取り有効
    tty.c_cflag &= ~(PARENB | PARODD);              // パリティ無効
    tty.c_cflag &= ~CSTOPB;                         // ストップビット1
    tty.c_cflag &= ~CRTSCTS;                        // ハードウェアフロー制御無効

    // 新しい設定を適用
    if (tcsetattr(serial_fd, TCSANOW, &tty) != 0) {
        perror("Error from tcsetattr");
        // atexit で close される
        return 1;
    }

    // ncurses 初期化
    initscr();
    if (has_colors() == FALSE) {
        endwin();
        printf("Your terminal does not support color\n");
        exit(1);
    }
    start_color();
    cbreak();
    noecho();
    curs_set(0); // カーソル非表示

    int term_rows, term_cols;
    getmaxyx(stdscr, term_rows, term_cols);

    // 色ペアの定義 (1番: 前景=緑, 背景=黒)
    init_pair(1, COLOR_GREEN, COLOR_BLACK);

    int ascii_win_height = term_rows / 2;
    int hex_win_height = term_rows - ascii_win_height;

    ascii_win = newwin(ascii_win_height, term_cols, 0, 0);
    hex_win = newwin(hex_win_height, term_cols, ascii_win_height, 0);

    scrollok(ascii_win, TRUE);
    scrollok(hex_win, TRUE);

    box(ascii_win, 0, 0);
    box(hex_win, 0, 0);

    mvwprintw(ascii_win, 0, 2, " ASCII Output (Port: %s) ", portname);
    mvwprintw(hex_win, 0, 2, " HEX Output ");
    mvwprintw(stdscr, term_rows -1, 1, "Press Ctrl+C to exit.");

    wrefresh(ascii_win);
    wrefresh(hex_win);
    refresh(); // stdscr の更新

    int hex_bytes_on_line = 0;
    const int MAX_HEX_BYTES_PER_LINE = 16; // 1行に表示する16進数のバイト数

    // データ受信ループ
    while (1) {
        n = read(serial_fd, buf, sizeof(buf) - 1); // バッファオーバーフローを防ぐために-1
        if (n < 0) {
            if (errno == EINTR) continue; // シグナルによる中断なら継続
            // ncurses終了前にエラーメッセージを表示したい場合
            // endwin(); // 一時的にncursesを終了
            // perror("Error reading from serial port");
            // refresh(); // initscr()を再度呼ぶ前に必要
            // initscr(); // 再開 (ただし、ウィンドウ再作成などが必要になる)
            // ここではシンプルにループを抜ける
            mvprintw(term_rows -1, 1, "Error reading from serial port. Exiting...");
            refresh();
            napms(2000); // 2秒待つ
            break; // エラー発生時はループを抜ける
        } else if (n > 0) {
            buf[n] = '\0'; // 受信データをNULL終端
            for (int i = 0; i < n; ++i) {
                // ASCII表示
                if (isprint((unsigned char)buf[i])) {
                    waddch(ascii_win, (unsigned char)buf[i]);
                } else if (buf[i] == '\n') {
                    waddch(ascii_win, '\n'); // scrollokが処理
                } else if (buf[i] == '\t') {
                    waddstr(ascii_win, "    "); // タブをスペース4つで簡易表示
                } else {
                    waddch(ascii_win, '.');
                }

                // HEX表示
                wattron(hex_win, COLOR_PAIR(1) | A_BOLD); // 色と太字を有効化
                wprintw(hex_win, "%02X ", (unsigned char)buf[i]);
                wattroff(hex_win, COLOR_PAIR(1) | A_BOLD); // 色と太字を無効化

                hex_bytes_on_line++;
                if (hex_bytes_on_line >= MAX_HEX_BYTES_PER_LINE) {
                    waddch(hex_win, '\n');
                    hex_bytes_on_line = 0;
                }
            }

            wrefresh(ascii_win);
            wrefresh(hex_win);
        }
        // n == 0 の場合はタイムアウト (VMIN=0, VTIME > 0 の場合)
        // タイムアウト時は何もせず次のreadへ
    }

    // cleanup_resources() が atexit により呼び出される
    return 0;
}