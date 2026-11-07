

typedef void (*nx_puts_t)(const char *);
typedef void (*nx_putc_t)(char);

#define NX_VERSION  "0.0.1"
#define NX_GH       "https://github.com/hi-its-matte"
#define NX_MAXDEPTH 64

static const char *p;    
static const char *err;  
static int depth;

static int expr(void);
static int unary(void);

static void skip(void) {
    while (*p == ' ' || *p == '\t') p++;
}

static int ipow(int b, int e) {
    int r = 1;
    if (e < 0) { err = "negative exponent"; return 0; }
    while (e) {
        if ((e & 1) && __builtin_mul_overflow(r, b, &r)) { err = "overflow"; return 0; }
        e >>= 1;
        if (e && __builtin_mul_overflow(b, b, &b)) { err = "overflow"; return 0; }
    }
    return r;
}

static int number(void) {
    int v = 0, any = 0, d;
    while (*p >= '0' && *p <= '9') {
        d = *p++ - '0';
        if (__builtin_mul_overflow(v, 10, &v) || __builtin_add_overflow(v, d, &v)) {
            err = "overflow";
            return 0;
        }
        any = 1;
    }
    if (any && (*p == '.' || *p == ',')) { err = "integers only"; return 0; }
    if (!any) err = "invalid operation";
    return v;
}

static int primary(void) {
    int v;
    skip();
    if (*p == '(') {
        if (++depth > NX_MAXDEPTH) { err = "too deep"; return 0; }
        p++;
        v = expr();
        depth--;
        if (err) return 0;
        skip();
        if (*p != ')') { err = "missing )"; return 0; }
        p++;
        return v;
    }
    return number();
}

static int power(void) {
    int b = primary(), e;
    if (err) return 0;
    skip();
    if (*p == '^' || (p[0] == '*' && p[1] == '*')) {
        p += (*p == '^') ? 1 : 2;
        if (++depth > NX_MAXDEPTH) { err = "too deep"; return 0; }
        e = unary();
        depth--;
        if (err) return 0;
        return ipow(b, e);
    }
    return b;
}

static int unary(void) {
    int v;
    char c;
    skip();
    if (*p == '-' || *p == '+') {
        c = *p++;
        if (++depth > NX_MAXDEPTH) { err = "too deep"; return 0; }
        v = unary();
        depth--;
        if (err) return 0;
        if (c == '-' && __builtin_sub_overflow(0, v, &v)) { err = "overflow"; return 0; }
        return v;
    }
    return power();
}

static int term(void) {
    int a = unary(), b, q, r, fl;
    char c;
    while (!err) {
        skip();
        c = *p;
        fl = 0;
        if (c == '*' && p[1] != '*') p++;
        else if (c == '/' || c == '%') {
            if (c == '/' && p[1] == '/') { fl = 1; p += 2; }
            else p++;
        } else break;
        b = unary();
        if (err) break;
        if (c == '*') {
            if (__builtin_mul_overflow(a, b, &a)) { err = "overflow"; break; }
            continue;
        }
        if (b == 0) { err = "division by zero"; break; }
        if (b == -1) { 
            if (c == '%') a = 0;
            else if (__builtin_sub_overflow(0, a, &a)) { err = "overflow"; break; }
            continue;
        }
        q = a / b;
        r = a % b;
        if (c == '%') {
            if (r != 0 && (r ^ b) < 0) r += b;   
            a = r;
        } else {
            if (fl && r != 0 && (r ^ b) < 0) q--; 
            a = q;
        }
    }
    return err ? 0 : a;
}

static int expr(void) {
    int a = term(), b;
    char c;
    while (!err) {
        skip();
        c = *p;
        if (c != '+' && c != '-') break;
        p++;
        b = term();
        if (err) break;
        if (c == '+' ? __builtin_add_overflow(a, b, &a)
                     : __builtin_sub_overflow(a, b, &a)) {
            err = "overflow";
            break;
        }
    }
    return err ? 0 : a;
}

static void put_int(int v, nx_puts_t out) {
    char buf[12];
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    int i = 11;
    buf[i] = '\0';
    do {
        buf[--i] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0) buf[--i] = '-';
    out(buf + i);
}

static int is_help(const char *s) {
    const char *h = "help";
    while (*h) if (*s++ != *h++) return 0;
    return *s == '\0' || *s == ' ' || *s == '\t';
}

void cmd_numix(const char *param, nx_puts_t vga_puts, nx_putc_t vga_putc) {
    int r;

    if (!param) param = "";
    while (*param == ' ' || *param == '\t') param++;

    if (*param == '\0') {
        vga_puts("Usage: numix [operation]");   vga_putc('\n');
        vga_puts("run 'numix help' for more");  vga_putc('\n');
        return;
    }
    if (is_help(param)) {
        vga_puts("Numix " NX_VERSION);                            vga_putc('\n');
        vga_puts("Integer calculator. Ops: + - * / // % ** ^ ( )"); vga_putc('\n');
        vga_puts("Example: numix 2+3*4  ->  14");                 vga_putc('\n');
        vga_puts("GitHub: " NX_GH);                               vga_putc('\n');
        return;
    }

    p = param;
    err = 0;
    depth = 0;
    r = expr();
    if (!err) { skip(); if (*p) err = "invalid operation"; }

    if (err) {
        vga_puts("Error: ");
        vga_puts(err);
        vga_putc('\n');
        return;
    }
    put_int(r, vga_puts);
    vga_putc('\n');
}