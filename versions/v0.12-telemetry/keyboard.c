#include "keyboard.h"
#include "io.h"
#include "telemetry.h"

#define KBD_DATA   0x60   /* 读：扫描码 */
#define KBD_STATUS 0x64   /* 读：状态，第 0 位 = 输出缓冲区里有数据 */

/* 扫描码第 1 套（PC/XT）：键盘控制器默认会把键盘的第 2 套翻译成这一套。
   下标是"按下"时的扫描码，松开时的扫描码 = 按下 | 0x80。0 = 不产生字符的键 */
static const char keymap_normal[58] = {
    0,    27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*',  0,   ' '
};

static const char keymap_shift[58] = {
    0,    27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*',  0,   ' '
};

#define SC_LSHIFT   0x2A
#define SC_RSHIFT   0x36
#define SC_CAPSLOCK 0x3A
#define SC_EXTENDED 0xE0   /* 方向键、右 Ctrl 等"扩展键"的前缀字节 */

/* 环形缓冲区：只有中断往里写（改 head），只有主循环往外读（改 tail）。
   单核、一写一读，不需要加锁 */
#define BUF_SIZE 128
static volatile char buf[BUF_SIZE];
static volatile unsigned int head, tail;

static int shift_down;      /* 左右 Shift 是否按着（两个键分开记，松开一个不影响另一个） */
static int lshift, rshift;
static int caps_lock;
static int extended;        /* 上一个字节是 0xE0，这一个字节属于扩展键 */

void keyboard_init(void) {
    int n = 0;
    while ((inb(KBD_STATUS) & 1) && n++ < 64) {
        (void) inb(KBD_DATA);   /* 把开机过程里积攒的字节读掉，否则第一次中断会读到旧数据 */
    }
}

static void buf_put(char c) {
    unsigned int next = (head + 1) % BUF_SIZE;
    if (next == tail) {         /* 满了就丢掉新字符，不覆盖还没读的 */
        tel_kbd_drop();
        return;
    }
    buf[head] = c;
    head = next;
    tel_kbd_char();
}

void keyboard_irq(void) {
    unsigned char sc = inb(KBD_DATA);
    int released = sc & 0x80;
    unsigned char code = sc & 0x7F;
    char c;

    if (sc == SC_EXTENDED) { extended = 1; return; }
    if (extended) { extended = 0; return; }   /* 方向键、右 Ctrl/Alt 等先不处理 */

    if (code == SC_LSHIFT) { lshift = !released; shift_down = lshift || rshift; return; }
    if (code == SC_RSHIFT) { rshift = !released; shift_down = lshift || rshift; return; }
    if (released) return;
    if (code == SC_CAPSLOCK) { caps_lock = !caps_lock; return; }
    if (code >= sizeof(keymap_normal)) return;

    c = shift_down ? keymap_shift[code] : keymap_normal[code];
    if (caps_lock && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
        c ^= 0x20;              /* Caps Lock 只翻转字母的大小写，Shift + Caps Lock 得到小写 */
    }
    if (c) buf_put(c);
}

int keyboard_getchar(void) {
    char c;
    if (tail == head) return -1;
    c = buf[tail];
    tail = (tail + 1) % BUF_SIZE;
    return (unsigned char) c;
}
