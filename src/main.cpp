#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <DHT.h>
#include "esp_task_wdt.h"

// --- KONFIGURASI PIN ---
#define DHTPIN 4      
#define DHTTYPE DHT11 
#define BTN_PIN 15    // Tombol untuk "SERANGAN HACKER"

// --- OBJEK SENSOR ---
DHT dht(DHTPIN, DHTTYPE);
Adafruit_INA219 ina219;

// --- LIBRARY KRIPTOGRAFI ---
#include "mbedtls/ecdsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ecp.h"

extern "C" {
    #include "sign.h"
    #include "randombytes.h"
    #include "api.h"
}

// --- VARIABEL GLOBAL CRYPTO ---
uint8_t pk_dili[PQCLEAN_MLDSA44_CLEAN_CRYPTO_PUBLICKEYBYTES];
uint8_t sk_dili[PQCLEAN_MLDSA44_CLEAN_CRYPTO_SECRETKEYBYTES];
uint8_t sig_dili[PQCLEAN_MLDSA44_CLEAN_CRYPTO_BYTES];
size_t sig_dili_len;

mbedtls_pk_context pk_ecdsa;
mbedtls_entropy_context entropy;
mbedtls_ctr_drbg_context ctr_drbg;
uint8_t sig_ecdsa[256];
size_t sig_ecdsa_len;

// --- RNG JEMBATAN ---
extern "C" void randombytes(uint8_t *out, size_t outlen) {
    uint32_t chunk;
    for (size_t i = 0; i < outlen; i += 4) {
        chunk = esp_random();
        if (i + 4 <= outlen) memcpy(out + i, &chunk, 4);
        else memcpy(out + i, &chunk, outlen - i);
    }
}

// --- HELPER: KONVERSI BYTE KE HEX STRING (Untuk Address/Signature) ---
String toHex(uint8_t *data, size_t len) {
    String buf = "";
    for (size_t i = 0; i < len; i++) {
        if (data[i] < 16) buf += "0";
        buf += String(data[i], HEX);
    }
    return buf;
}

// --- FUNGSI PROSES SIGNING & KIRIM DATA ---
void run_benchmark(String alg_name, String data_payload, float power_now) {
    unsigned long t_start, t_end;
    String signature_snippet = "";
    String address_snippet = ""; // Public Key ID

    // Matikan interrupt agar waktu akurat
    noInterrupts();
    t_start = micros();

    if (alg_name == "ML-DSA-44") {
        PQCLEAN_MLDSA44_CLEAN_crypto_sign(sig_dili, &sig_dili_len, (uint8_t*)data_payload.c_str(), data_payload.length(), sk_dili);
        
        // Ambil 4 byte awal Signature & Public Key sebagai identitas
        signature_snippet = toHex(sig_dili, 4); 
        address_snippet = toHex(pk_dili, 4);    
    } 
    else if (alg_name == "ECDSA") {
        uint8_t hash[32];
        mbedtls_sha256((const unsigned char*)data_payload.c_str(), data_payload.length(), hash, 0);
        mbedtls_pk_sign(&pk_ecdsa, MBEDTLS_MD_SHA256, hash, 0, sig_ecdsa, &sig_ecdsa_len, mbedtls_ctr_drbg_random, &ctr_drbg);
        
        // Ambil 4 byte awal Signature
        signature_snippet = toHex(sig_ecdsa, 4);
        // ECDSA Public Key agak ribet diambil raw-nya, kita buat dummy ID dari hash key
        address_snippet = "EC256"; 
    }

    t_end = micros();
    interrupts();

    unsigned long latency = t_end - t_start;

    // FORMAT BARU: LOG:ALG|DATA|WAKTU|DAYA|SIGNATURE_ID|ADDRESS_ID
    Serial.printf("LOG:%s|%s|%lu|%.2f|%s|%s\n", 
        alg_name.c_str(), 
        data_payload.c_str(), 
        latency, 
        power_now,
        signature_snippet.c_str(),
        address_snippet.c_str()
    );
}

// --- TASK UTAMA (STACK 60KB) ---
void TaskCrypto(void *pvParameters) {
    Serial.println("\n--- [SYSTEM] MEMULAI TASK SYSTEM (AUTO-MODE) ---");

    dht.begin();
    if (!ina219.begin()) Serial.println("WARNING: INA219 Error/Not Found");

    // 1. GENERATE KUNCI
    Serial.print("[KEYGEN] ML-DSA-44... ");
    PQCLEAN_MLDSA44_CLEAN_crypto_sign_keypair(pk_dili, sk_dili);
    Serial.println("DONE.");

    Serial.print("[KEYGEN] ECDSA... ");
    mbedtls_pk_init(&pk_ecdsa);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);
    const char *pers = "hacker_proof";
    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers, strlen(pers));
    mbedtls_pk_setup(&pk_ecdsa, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(pk_ecdsa), mbedtls_ctr_drbg_random, &ctr_drbg);
    Serial.println("DONE.\n");

    Serial.println(">> SISTEM OTOMATIS BERJALAN <<");
    Serial.println(">> TEKAN TOMBOL UNTUK SIMULASI PERETASAN (HACKING) <<");

    // 2. LOOP OTOMATIS (Non-Stop)
    while(true) {
        // A. Cek Status Hacker (Tombol)
        bool is_hacked = (digitalRead(BTN_PIN) == LOW);
        
        // B. Baca Sensor
        float t_asli = dht.readTemperature();
        if (isnan(t_asli)) t_asli = 0.0;
        
        String data_final;
        
        if (is_hacked) {
            // SKENARIO HACKING: Data dimanipulasi
            data_final = "99.99"; 
            Serial.println("\n[ALERT] SERANGAN HACKER TERDETEKSI! Memalsukan data...");
        } else {
            // NORMAL
            data_final = String(t_asli, 2);
            Serial.println("\n[INFO] Mengirim Data Normal...");
        }

        float power_mw = ina219.getPower_mW();

        // C. Kirim Data (ECDSA & ML-DSA)
        run_benchmark("ECDSA", data_final, power_mw);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        
        run_benchmark("ML-DSA-44", data_final, power_mw);

        // D. Jeda 5 Detik sebelum kirim lagi
        vTaskDelay(5000 / portTICK_PERIOD_MS); 
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(BTN_PIN, INPUT_PULLUP);
    disableCore0WDT();
    
    xTaskCreatePinnedToCore(TaskCrypto, "CryptoTask", 60 * 1024, NULL, 1, NULL, 1);
}

void loop() {
    vTaskDelete(NULL);
}