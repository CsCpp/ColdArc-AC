// =============================================================================
// НАСТРОЙКИ КОНФИГУРАЦИИ (FUSES) - СТРОГО ДО ИНКЛУДОВ
// =============================================================================
#pragma config FOSC = INTRC_NOCLKOUT, WDTE = OFF, PWRTE = OFF, MCLRE = ON
#pragma config CP = OFF, CPD = OFF, BOREN = ON, IESO = OFF, FCMEN = OFF
#pragma config LVP = OFF, BOR4V = BOR40V, WRT = OFF

#include <xc.h>
#include <stdint.h>

#define _XTAL_FREQ 8000000

// =============================================================================
// АППАРАТНАЯ ПРИВЯЗКА (HARDWARE MAPPING)
// =============================================================================
// Полумост A316J
#define AC_HIGH    PORTCbits.RC0 // Прямая полярность (EP)
#define AC_LOW     PORTCbits.RC1 // Обратная полярность (EN)

// Аноды дисплея (4 разряда)
#define AN_1 PORTCbits.RC4
#define AN_2 PORTCbits.RC5
#define AN_3 PORTCbits.RC6
#define AN_4 PORTCbits.RC7

// Входы управления
#define BTN_START  PORTBbits.RB0 // Сигнал WELD от первой платы (0 - Сварка)
#define ENC_BTN    PORTBbits.RB3 // Кнопка энкодера (Смена параметра)
#define ENC_A      PORTBbits.RB4 // Энкодер пин А (IOC)
#define ENC_B      PORTBbits.RB5 // Энкодер пин B (IOC)

// =============================================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ И НАСТРОЙКИ
// =============================================================================
// Значения по умолчанию
uint8_t  cfg_pwm_max = 80;   // A: Мощность / Ширина импульса (10-100%)
uint16_t cfg_freq    = 100;  // F: Частота AC (20-200 Гц)
uint8_t  cfg_balance = 30;   // b: Баланс EP / Очистка (10-50%)
uint8_t  cfg_up_slp  = 10;   // U: Нарастание (0-50 -> 0.0 - 5.0 сек)
uint8_t  cfg_dn_slp  = 20;   // d: Спад / Заварка кратера (0-50 -> 0.0 - 5.0 сек)
uint8_t  cfg_ign_ms  = 20;   // H: Горячий старт / Прогрев (0-100 мс)

uint8_t menu_idx = 0;        // Индекс текущего меню (0..5)
volatile uint8_t enc_last = 0;

// Маски для Общего Анода (сегменты на PORTA, DP не используется)
const uint8_t digits[] = { 0xC0, 0xF9, 0xA4, 0xB0, 0x99, 0x92, 0x82, 0xF8, 0x80, 0x90 };

// Символы меню: A, F, b, U, d, H
const uint8_t mode_chars[] = { 0x88, 0x8E, 0x83, 0xC1, 0xA1, 0x89 };

// =============================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// =============================================================================
// Функция задержки с переменной (шаг 10 мкс)
void delay_10us(uint16_t tens_of_us) {
    while(tens_of_us--) {
        __delay_us(10);
    }
}

// =============================================================================
// ПРЕРЫВАНИЯ (Индикация и Энкодер)
// =============================================================================
void __interrupt() ISR() {
    // 1. ДИНАМИЧЕСКАЯ ИНДИКАЦИЯ (Таймер 0)
    if (T0IF) {
        static uint8_t digit = 0;
        uint16_t val = 0;
        
        // Подготовка значения в зависимости от меню
        switch(menu_idx) {
            case 0: val = cfg_pwm_max; break;
            case 1: val = cfg_freq; break;
            case 2: val = cfg_balance; break;
            case 3: val = cfg_up_slp; break;
            case 4: val = cfg_dn_slp; break;
            case 5: val = cfg_ign_ms; break;
        }
        
        PORTC &= 0x0F; // Гасим все аноды (обнуляем RC4-RC7)
        
        if (digit == 0) { PORTA = mode_chars[menu_idx]; AN_1 = 1; }
        if (digit == 1) { PORTA = digits[(val / 100) % 10]; AN_2 = 1; }
        if (digit == 2) { PORTA = digits[(val / 10) % 10];  AN_3 = 1; }
        if (digit == 3) { PORTA = digits[val % 10];         AN_4 = 1; }
        
        if (++digit > 3) digit = 0;
        
        TMR0 = 100;
        T0IF = 0;
    }

    // 2. ОБРАБОТКА ЭНКОДЕРА (IOC на PORTB)
    if (RBIF) {
        uint8_t curr = (PORTB >> 4) & 0x03; // Читаем RB4 и RB5
        if (enc_last == 0x03) {
            if (curr == 0x01) { // Поворот Вправо (+)
                if (menu_idx == 0 && cfg_pwm_max < 100) cfg_pwm_max++;
                if (menu_idx == 1 && cfg_freq < 200)    cfg_freq++;
                if (menu_idx == 2 && cfg_balance < 50)  cfg_balance++;
                if (menu_idx == 3 && cfg_up_slp < 50)   cfg_up_slp++;
                if (menu_idx == 4 && cfg_dn_slp < 50)   cfg_dn_slp++;
                if (menu_idx == 5 && cfg_ign_ms < 100)  cfg_ign_ms++;
            }
            if (curr == 0x02) { // Поворот Влево (-)
                if (menu_idx == 0 && cfg_pwm_max > 10) cfg_pwm_max--;
                if (menu_idx == 1 && cfg_freq > 20)    cfg_freq--;
                if (menu_idx == 2 && cfg_balance > 10) cfg_balance--;
                if (menu_idx == 3 && cfg_up_slp > 0)   cfg_up_slp--;
                if (menu_idx == 4 && cfg_dn_slp > 0)   cfg_dn_slp--;
                if (menu_idx == 5 && cfg_ign_ms > 0)   cfg_ign_ms--;
            }
        }
        enc_last = curr;
        RBIF = 0;
    }
}

// =============================================================================
// ОСНОВНАЯ ПРОГРАММА
// =============================================================================
void main() {
    OSCCON = 0x71; // 8 МГц
    ANSEL = 0; ANSELH = 0;
    
    TRISA = 0x00; // Сегменты
    TRISC = 0x00; // Аноды и ключи
    TRISB = 0xFF; // Кнопки и энкодер
    
    OPTION_REGbits.nRBPU = 0; WPUB = 0xFF; // Подтяжки
    
    OPTION_REGbits.T0CS = 0; OPTION_REGbits.PSA = 0; OPTION_REGbits.PS = 0b011; // Таймер 0
    IOCBbits.IOCB4 = 1; IOCBbits.IOCB5 = 1; // Прерывания энкодера
    
    AC_HIGH = 0; AC_LOW = 0; PORTA = 0xFF; PORTC = 0x00;
    
    T0IE = 1; RBIE = 1; GIE = 1;

    uint8_t btn_lock = 0;

    while(1) {
        // --- ОБРАБОТКА КНОПКИ МЕНЮ ---
        if (ENC_BTN == 0 && !btn_lock) {
            if (++menu_idx > 5) menu_idx = 0;
            btn_lock = 1;
        } else if (ENC_BTN == 1) {
            btn_lock = 0;
        }

        // --- ЖЕСТКИЙ ОПРОС СИГНАЛА СВАРКИ ---
        if (BTN_START == 0) {
            // 1. ПОЛНАЯ БЛОКИРОВКА ПРЕРЫВАНИЙ
            GIE = 0; 
            
            // 2. ИНДИКАЦИЯ "8888" (статично)
            PORTC |= 0xF0; // Аноды ВКЛ
            PORTA = 0x00;  // Все сегменты ВКЛ (Общий Анод)

            // 3. РАСЧЕТ ТАЙМИНГОВ БАЗЫ (в единицах по 10 мкс)
            uint32_t T_tot_10us = 100000 / cfg_freq; // Период
            uint32_t T_ep_10us  = (T_tot_10us * cfg_balance) / 100; // Очистка
            uint32_t T_en_10us  = T_tot_10us - T_ep_10us; // Сварка
            
            // Расчет времени спада/нарастания
            uint32_t up_ms_total = cfg_up_slp * 100;
            uint32_t dn_ms_total = cfg_dn_slp * 100;
            uint32_t cycle_ms = 1000 / cfg_freq;
            
            uint32_t elapsed_ms = 0;
            uint16_t current_pwm = (cfg_up_slp == 0) ? cfg_pwm_max : 0;
            uint8_t  is_down_slope = 0;

            // 4. ГОРЯЧИЙ СТАРТ (Жесткий DC импульс для разогрева вольфрама)
            if (cfg_ign_ms > 0) {
                AC_HIGH = 1; AC_LOW = 0;
                for(uint8_t i = 0; i < cfg_ign_ms; i++) __delay_ms(1);
                AC_HIGH = 0;
            }

            // 5. ГЛАВНЫЙ ЦИКЛ СВАРКИ
            while (1) {
                // Проверка отпускания кнопки -> старт заварки кратера
                if (BTN_START == 1 && is_down_slope == 0) {
                    if (cfg_dn_slp == 0) break; // Сразу стоп
                    is_down_slope = 1;
                    elapsed_ms = 0; // Сброс таймера для спада
                }

                // Математика ШИМ (Софт-старт и Софт-стоп)
                if (is_down_slope == 1) { // Заварка кратера
                    elapsed_ms += cycle_ms;
                    if (elapsed_ms >= dn_ms_total) break; // Завершение цикла
                    current_pwm = cfg_pwm_max - ((uint32_t)cfg_pwm_max * elapsed_ms) / dn_ms_total;
                } else { // Нарастание или Рабочий режим
                    if (elapsed_ms < up_ms_total) {
                        elapsed_ms += cycle_ms;
                        current_pwm = ((uint32_t)cfg_pwm_max * elapsed_ms) / up_ms_total;
                    } else {
                        current_pwm = cfg_pwm_max; // Достигли максимума
                    }
                }

                // Расчет длительности импульсов с учетом ШИМ
                uint32_t t_ep_on  = (T_ep_10us * current_pwm) / 100;
                uint32_t t_ep_off = T_ep_10us - t_ep_on;
                
                uint32_t t_en_on  = (T_en_10us * current_pwm) / 100;
                uint32_t t_en_off = T_en_10us - t_en_on;

                // --- ФАЗА 1: EP (Очистка) ---
                AC_LOW = 0; 
                __delay_us(5); // DEAD-TIME (Аппаратная защита драйверов)
                if (t_ep_on > 0) {
                    AC_HIGH = 1;
                    delay_10us(t_ep_on);
                    AC_HIGH = 0;
                }
                if (t_ep_off > 0) delay_10us(t_ep_off);

                // --- ФАЗА 2: EN (Проплавление) ---
                AC_HIGH = 0;
                __delay_us(5); // DEAD-TIME (Аппаратная защита драйверов)
                if (t_en_on > 0) {
                    AC_LOW = 1;
                    delay_10us(t_en_on);
                    AC_LOW = 0;
                }
                if (t_en_off > 0) delay_10us(t_en_off);
            }

            // 6. БЕЗОПАСНЫЙ ВЫХОД
            AC_HIGH = 0; 
            AC_LOW = 0; 
            
            PORTC &= 0x0F; // Гасим аноды
            GIE = 1;       // ВОЗВРАЩАЕМ ПРЕРЫВАНИЯ
        }
    }
}
