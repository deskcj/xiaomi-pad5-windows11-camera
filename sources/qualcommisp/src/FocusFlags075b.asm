        AREA |.text|, CODE, READONLY, ALIGN=2
        EXPORT FocusFlags075b
FocusFlags075b PROC
        ; Replaces 16d91c..16d974, within the existing GET function.
        ; w9 = current AF mode; x22 = CaptureProperties; x20 = property.
        ; Preserve non-mode/range bits, including AUTO lock. Continuous
        ; must not carry the mutually exclusive legacy lock bit.
        ldr     x8, [x20, #0x70]
        mov     x10, #0x103
        movk    x10, #0x1f, lsl #16
        bic     x8, x8, x10
        sub     w10, w9, #3
        cmp     w10, #1
        orr     x5, x8, #1
        orr     x10, x8, #0x100
        bic     x10, x10, #4
        csel    x5, x10, x5, ls
        ldr     w8, [x22, #0xbc]
        mov     x10, #0x20000
        mov     x11, #0x10000
        cmp     w8, #1
        csel    x10, x11, x10, eq
        mov     x11, #0x40000
        cmp     w8, #2
        csel    x10, x11, x10, eq
        orr     x5, x5, x10
        str     x5, [x20, #0x70]
        nop
        nop
        nop
        ; Falls through to original 16d978. Manual/fixed-focus paths bypass.
FocusFlags075b ENDP
        END
