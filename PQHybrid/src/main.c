#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pqcuda.h"

typedef struct {
    double average;
    double p50;
    double p95;
    double throughput;
} benchmark_stats;

typedef struct {
    size_t batch;
    benchmark_stats stats;
} benchmark_result;

static int read_choice(const char *prompt) {
    char line[128];
    int value;
    char extra;
    fputs(prompt, stdout);
    fflush(stdout);
    if (fgets(line, sizeof(line), stdin) == NULL) return -1;
    return sscanf(line, " %d %c", &value, &extra) == 1 ? value : -1;
}

static void print_hex(const char *label, const uint8_t *data, size_t size) {
    size_t i;
    printf("%s (%zu bytes):\n", label, size);
    for (i = 0; i < size; ++i) printf("%02x", (unsigned int)data[i]);
    putchar('\n');
}

static int write_hex_file(const char *path, const uint8_t *data, size_t size) {
    size_t i;
    FILE *output = fopen(path, "w");
    if (output == NULL) return 0;
    for (i = 0; i < size; ++i) fprintf(output, "%02x", (unsigned int)data[i]);
    fputc('\n', output);
    return fclose(output) == 0;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static int append_nonspace(char *hex, size_t capacity, size_t *length,
                           const char *text) {
    while (*text != '\0') {
        if (!isspace((unsigned char)*text)) {
            if (*length >= capacity) return 0;
            hex[(*length)++] = *text;
        }
        ++text;
    }
    return 1;
}

static int read_hex(const char *label, size_t size, uint8_t **out) {
    char *line = NULL;
    char *hex = NULL;
    size_t line_capacity = 0;
    size_t hex_length = 0;
    size_t expected = size * 2;
    ssize_t line_length;
    int ok = 0;
    size_t i;

    printf("%s를 입력하세요 (%zu bytes, %zu hex characters).\n"
           "여러 줄로 입력하거나 @파일명으로 불러올 수 있습니다:\n",
           label, size, expected);
    line_length = getline(&line, &line_capacity, stdin);
    if (line_length < 0) goto done;
    hex = (char *)malloc(expected + 3);
    if (hex == NULL) goto done;

    if (line[0] == '@') {
        FILE *input;
        int c;
        char *newline = strpbrk(line + 1, "\r\n");
        if (newline != NULL) *newline = '\0';
        input = fopen(line + 1, "r");
        if (input == NULL) {
            fprintf(stderr, "파일을 열 수 없습니다: %s\n", line + 1);
            goto done;
        }
        while ((c = fgetc(input)) != EOF) {
            if (!isspace((unsigned char)c)) {
                if (hex_length >= expected + 2) {
                    fclose(input);
                    goto invalid_length;
                }
                hex[hex_length++] = (char)c;
            }
        }
        fclose(input);
    } else {
        for (;;) {
            if (!append_nonspace(hex, expected + 2, &hex_length, line))
                goto invalid_length;
            if (hex_length >= expected) break;
            printf("  %zu/%zu hex characters 입력됨. 계속 입력하세요:\n",
                   hex_length, expected);
            line_length = getline(&line, &line_capacity, stdin);
            if (line_length < 0) goto done;
        }
    }
    if (hex_length >= 2 && hex[0] == '0' &&
        (hex[1] == 'x' || hex[1] == 'X')) {
        memmove(hex, hex + 2, hex_length - 2);
        hex_length -= 2;
    }
    if (hex_length != expected) goto invalid_length;

    *out = (uint8_t *)malloc(size);
    if (*out == NULL) goto done;
    for (i = 0; i < size; ++i) {
        int hi = hex_value(hex[2 * i]);
        int lo = hex_value(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            fprintf(stderr, "hex가 아닌 문자가 포함되어 있습니다.\n");
            free(*out);
            *out = NULL;
            goto done;
        }
        (*out)[i] = (uint8_t)((hi << 4) | lo);
    }
    ok = 1;
    goto done;

invalid_length:
    fprintf(stderr, "입력 길이가 올바르지 않습니다.\n");
done:
    free(hex);
    free(line);
    return ok;
}

static int read_message(uint8_t **message, size_t *message_length) {
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    fputs("메시지를 입력하세요: ", stdout);
    fflush(stdout);
    length = getline(&line, &capacity, stdin);
    if (length < 0) {
        free(line);
        return 0;
    }
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
        --length;
    *message = (uint8_t *)line;
    *message_length = (size_t)length;
    return 1;
}

static double elapsed_ms(struct timespec start, struct timespec end) {
    return (double)(end.tv_sec - start.tv_sec) * 1000.0 +
           (double)(end.tv_nsec - start.tv_nsec) / 1000000.0;
}

static int compare_double(const void *left, const void *right) {
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}

static benchmark_stats summarize(double *samples, size_t sample_count,
                                 size_t operations) {
    benchmark_stats stats;
    double sum = 0.0;
    size_t i;
    for (i = 0; i < sample_count; ++i) samples[i] /= (double)operations;
    qsort(samples, sample_count, sizeof(*samples), compare_double);
    for (i = 0; i < sample_count; ++i) sum += samples[i];
    stats.average = sum / (double)sample_count;
    stats.p50 = samples[(size_t)(0.50 * (double)(sample_count - 1))];
    stats.p95 = samples[(size_t)(0.95 * (double)(sample_count - 1))];
    stats.throughput = (double)sample_count * 1000.0 / sum;
    return stats;
}

static void print_all_benchmark_results(const char *algorithm, const char *name,
                                        const benchmark_result *results,
                                        size_t result_count) {
    size_t i;
    printf("\n%s %s benchmark results:\n", algorithm, name);
    for (i = 0; i < result_count; ++i) {
        puts("+------------+------------------+--------------------+");
        puts("| Batch      | Latency          | Throughput         |");
        puts("+------------+------------------+--------------------+");
        printf("| %-10zu | %11.6f ms  | %12.3f ops/s |\n",
               results[i].batch, results[i].stats.average,
               results[i].stats.throughput);
        puts("+------------+------------------+--------------------+");
    }
    putchar('\n');
}

static int run_kyber_benchmark(void) {
    static const char *names[] = {"", "keypair", "encaps", "decaps"};
    uint8_t pk[PQCUDA_KYBER1024_PUBLIC_KEY_BYTES];
    uint8_t sk[PQCUDA_KYBER1024_SECRET_KEY_BYTES];
    uint8_t ct[PQCUDA_KYBER1024_CIPHERTEXT_BYTES];
    uint8_t ss[PQCUDA_KYBER1024_SHARED_SECRET_BYTES];
    benchmark_result result;
    double samples[5];
    size_t sample_count = 0;
    size_t i;
    int run;
    int selected;

    printf("\nML-KEM Benchmark 작업을 선택하세요.\n"
           "  1. keypair\n  2. encaps\n  3. decaps\n");
    selected = read_choice("선택: ");
    if (selected < 1 || selected > 3) return 2;
    printf("\nML-KEM %s benchmark (latency ms / throughput ops/s)\n",
           names[selected]);

    puts("Kyber1024 kernel별 threads/block 튜닝 중 "
         "(batch=1, 후보=32/64/128/192/256)...");
    fflush(stdout);
    if (pqcuda_kyber1024_tune_launch_profile() != 0) return 1;
    puts("선택된 Kyber1024 launch profile:");
    for (i = 0; i < pqcuda_kyber1024_tuned_kernel_count(); ++i) {
        printf("  %-24s %zu threads/block\n",
               pqcuda_kyber1024_tuned_kernel_name(i),
               pqcuda_kyber1024_tuned_kernel_threads(i));
    }

    if (selected != 1 && pqcuda_kyber1024_keypair(pk, sk) != 0) return 1;
    if (selected == 3 && pqcuda_kyber1024_encapsulate(ct, ss, pk) != 0)
        return 1;

    printf("Benchmarking Kyber1024 %s with tuned profile (batch=1)...\n",
           names[selected]);
    fflush(stdout);
    for (run = 0; run < 6; ++run) {
        struct timespec start, end;
        int call_result = 0;
        clock_gettime(CLOCK_MONOTONIC, &start);
        if (selected == 1)
            call_result = pqcuda_kyber1024_keypair(pk, sk);
        else if (selected == 2)
            call_result = pqcuda_kyber1024_encapsulate(ct, ss, pk);
        else
            call_result = pqcuda_kyber1024_decapsulate(ss, ct, sk);
        clock_gettime(CLOCK_MONOTONIC, &end);
        if (call_result != 0) return 1;
        if (run != 0) samples[sample_count++] = elapsed_ms(start, end);
    }
    result.batch = 1;
    result.stats = summarize(samples, sample_count, 1);
    print_all_benchmark_results("ML-KEM", names[selected], &result, 1);
    return 0;
}

static int run_dilithium_benchmark(void) {
    static const uint8_t message[] = {'P', 'Q', 'C', 'U', 'D', 'A'};
    static const char *names[] = {"", "keypair", "sign", "verify"};
    const size_t batch = 1;
    benchmark_result result;
    size_t pk_size;
    size_t sk_size;
    size_t sig_size;
    uint8_t *pk;
    uint8_t *sk;
    uint8_t *sig;
    size_t sig_length = 0;
    double samples[5];
    size_t sample_count = 0;
    size_t i;
    int run;
    int selected, operation;
    pqcuda_dilithium_mode mode;

    printf("\nML-DSA mode를 선택하세요.\n  2. Dilithium-2\n"
           "  3. Dilithium-3\n  5. Dilithium-5\n");
    selected = read_choice("선택: ");
    if (selected != 2 && selected != 3 && selected != 5) return 2;
    mode = (pqcuda_dilithium_mode)selected;
    printf("\nBenchmark 작업을 선택하세요.\n"
           "  1. keypair\n  2. sign\n  3. verify\n");
    operation = read_choice("선택: ");
    if (operation < 1 || operation > 3) return 2;

    printf("\nML-DSA-%d kernel variant 튜닝 중 (batch=1)...\n", selected);
    fflush(stdout);
    if (pqcuda_dilithium_tune_sign_kernels(mode) != 0) return 1;
    puts("선택된 ML-DSA signing kernel variants:");
    for (i = 0; i < pqcuda_dilithium_tuned_stage_count(mode); ++i) {
        printf("  %-16s %s\n",
               pqcuda_dilithium_tuned_stage_name(mode, i),
               pqcuda_dilithium_tuned_variant_name(mode, i));
    }

    pk_size = pqcuda_dilithium_public_key_bytes(mode);
    sk_size = pqcuda_dilithium_secret_key_bytes(mode);
    sig_size = pqcuda_dilithium_signature_bytes(mode);
    pk = (uint8_t *)malloc(pk_size);
    sk = (uint8_t *)malloc(sk_size);
    sig = (uint8_t *)malloc(sig_size);
    if (pk == NULL || sk == NULL || sig == NULL) {
        free(pk); free(sk); free(sig);
        return 1;
    }
    if (pqcuda_dilithium_keypair_batch(
            mode, pk, pk_size, sk, sk_size, batch) != 0 ||
        pqcuda_dilithium_sign_batch(
            mode, sig, sig_size, &sig_length,
            message, sizeof(message), sk, sk_size, batch) != 0) {
        free(pk); free(sk); free(sig);
        return 1;
    }

    printf("Benchmarking ML-DSA-%d %s with tuned variants (batch=1)...\n",
           selected, names[operation]);
    fflush(stdout);
    for (run = 0; run < 6; ++run) {
        struct timespec start, end;
        int call_result = 0;
        clock_gettime(CLOCK_MONOTONIC, &start);
        if (operation == 1)
            call_result = pqcuda_dilithium_keypair_batch(
                mode, pk, pk_size, sk, sk_size, batch);
        else if (operation == 2)
            call_result = pqcuda_dilithium_sign_batch(
                mode, sig, sig_size, &sig_length,
                message, sizeof(message), sk, sk_size, batch);
        else
            call_result = pqcuda_dilithium_verify_batch(
                mode, sig, sig_length, message, sizeof(message),
                pk, pk_size, batch);
        clock_gettime(CLOCK_MONOTONIC, &end);
        if (call_result != 0) {
            free(pk); free(sk); free(sig);
            return 1;
        }
        if (run != 0) samples[sample_count++] = elapsed_ms(start, end);
    }
    result.batch = batch;
    result.stats = summarize(samples, sample_count, batch);
    print_all_benchmark_results("ML-DSA", names[operation], &result, 1);
    free(pk); free(sk); free(sig);
    return 0;
}

static int run_kyber(void) {
    int op;
    printf("\n보안 레벨을 선택하세요.\n  1. Kyber1024\n");
    if (read_choice("선택: ") != 1) {
        fprintf(stderr, "지원하지 않는 보안 레벨입니다.\n");
        return 2;
    }
    printf("\n작업을 선택하세요.\n  1. keypair\n  2. encaps\n  3. decaps\n");
    op = read_choice("선택: ");
    if (op == 1) {
        uint8_t pk[PQCUDA_KYBER1024_PUBLIC_KEY_BYTES];
        uint8_t sk[PQCUDA_KYBER1024_SECRET_KEY_BYTES];
        if (pqcuda_kyber1024_keypair(pk, sk) != 0) return 1;
        if (!write_hex_file("kyber1024_public_key.hex", pk, sizeof(pk)) ||
            !write_hex_file("kyber1024_private_key.hex", sk, sizeof(sk))) {
            fprintf(stderr, "KeyGen 결과 파일 저장에 실패했습니다.\n");
            return 1;
        }
        printf("Public key (%zu bytes): kyber1024_public_key.hex\n"
               "Private key (%zu bytes): kyber1024_private_key.hex\n",
               sizeof(pk), sizeof(sk));
        return 0;
    }
    if (op == 2) {
        uint8_t *pk = NULL;
        uint8_t ct[PQCUDA_KYBER1024_CIPHERTEXT_BYTES];
        uint8_t ss[PQCUDA_KYBER1024_SHARED_SECRET_BYTES];
        int result;
        if (!read_hex("Public key", PQCUDA_KYBER1024_PUBLIC_KEY_BYTES, &pk))
            return 2;
        result = pqcuda_kyber1024_encapsulate(ct, ss, pk);
        free(pk);
        if (result != 0) return 1;
        if (write_hex_file("kyber1024_ciphertext.hex", ct, sizeof(ct)) &&
            write_hex_file("kyber1024_shared_key_encap.hex", ss, sizeof(ss)))
            printf("Ciphertext (%zu bytes): kyber1024_ciphertext.hex\n"
                   "Shared secret (%zu bytes): kyber1024_shared_key_encap.hex\n",
                   sizeof(ct), sizeof(ss));
        else
            fprintf(stderr, "Encapsulation 결과 파일 저장에 실패했습니다.\n");
        return 0;
    }
    if (op == 3) {
        uint8_t *ct = NULL;
        uint8_t *sk = NULL;
        uint8_t ss[PQCUDA_KYBER1024_SHARED_SECRET_BYTES];
        int result;
        if (!read_hex("Ciphertext", PQCUDA_KYBER1024_CIPHERTEXT_BYTES, &ct) ||
            !read_hex("Private key", PQCUDA_KYBER1024_SECRET_KEY_BYTES, &sk)) {
            free(ct); free(sk);
            return 2;
        }
        result = pqcuda_kyber1024_decapsulate(ss, ct, sk);
        free(ct); free(sk);
        if (result != 0) return 1;
        print_hex("Shared secret", ss, sizeof(ss));
        if (write_hex_file("kyber1024_shared_key_decap.hex", ss, sizeof(ss)))
            printf("결과를 kyber1024_shared_key_decap.hex에 저장했습니다.\n");
        else
            fprintf(stderr, "Decapsulation 결과 파일 저장에 실패했습니다.\n");
        return 0;
    }
    fprintf(stderr, "지원하지 않는 작업입니다.\n");
    return 2;
}

static int run_dilithium(void) {
    int selected, op;
    pqcuda_dilithium_mode mode;
    size_t pk_size, sk_size, sig_size;
    char public_key_file[64], private_key_file[64], signature_file[64];
    printf("\nML-DSA 보안 레벨(mode)을 선택하세요.\n"
           "  2. Dilithium-2\n  3. Dilithium-3\n  5. Dilithium-5\n");
    selected = read_choice("선택: ");
    if (selected != 2 && selected != 3 && selected != 5) {
        fprintf(stderr, "지원하지 않는 mode입니다.\n");
        return 2;
    }
    mode = (pqcuda_dilithium_mode)selected;
    pk_size = pqcuda_dilithium_public_key_bytes(mode);
    sk_size = pqcuda_dilithium_secret_key_bytes(mode);
    sig_size = pqcuda_dilithium_signature_bytes(mode);
    snprintf(public_key_file, sizeof(public_key_file),
             "cudilithium%d_public_key.hex", selected);
    snprintf(private_key_file, sizeof(private_key_file),
             "cudilithium%d_private_key.hex", selected);
    snprintf(signature_file, sizeof(signature_file),
             "cudilithium%d_signature.hex", selected);
    printf("\n작업을 선택하세요.\n  1. keypair\n  2. sign\n  3. verify\n");
    op = read_choice("선택: ");
    if (op == 1) {
        uint8_t *pk = (uint8_t *)malloc(pk_size);
        uint8_t *sk = (uint8_t *)malloc(sk_size);
        int result;
        if (pk == NULL || sk == NULL) { free(pk); free(sk); return 1; }
        result = pqcuda_dilithium_keypair(mode, pk, pk_size, sk, sk_size);
        if (result == 0 &&
            (!write_hex_file(public_key_file, pk, pk_size) ||
             !write_hex_file(private_key_file, sk, sk_size))) result = -1;
        free(pk); free(sk);
        if (result != 0) {
            fprintf(stderr, "KeyGen 결과 파일 저장 또는 생성에 실패했습니다.\n");
            return 1;
        }
        printf("Public key (%zu bytes): %s\nPrivate key (%zu bytes): %s\n",
               pk_size, public_key_file, sk_size, private_key_file);
        return 0;
    }
    if (op == 2) {
        uint8_t *message = NULL, *sk = NULL;
        uint8_t *sig = (uint8_t *)malloc(sig_size);
        size_t message_length = 0, sig_length = 0;
        int result;
        if (sig == NULL || !read_message(&message, &message_length) ||
            !read_hex("Private key", sk_size, &sk)) {
            free(message); free(sk); free(sig);
            return 2;
        }
        result = pqcuda_dilithium_sign(mode, sig, sig_size, &sig_length,
                                       message, message_length, sk, sk_size);
        if (result == 0 && !write_hex_file(signature_file, sig, sig_length))
            result = -1;
        free(message); free(sk); free(sig);
        if (result != 0) return 1;
        printf("Signature (%zu bytes): %s\n", sig_length, signature_file);
        return 0;
    }
    if (op == 3) {
        uint8_t *message = NULL, *pk = NULL, *sig = NULL;
        size_t message_length = 0;
        int result;
        if (!read_message(&message, &message_length) ||
            !read_hex("Public key", pk_size, &pk) ||
            !read_hex("Signature", sig_size, &sig)) {
            free(message); free(pk); free(sig);
            return 2;
        }
        result = pqcuda_dilithium_verify(
            mode, sig, sig_size, message, message_length, pk, pk_size);
        free(message); free(pk); free(sig);
        printf("%s", result == 0 ? "Signature: VALID\n" : "Signature: INVALID\n");
        return result == 0 ? 0 : 1;
    }
    fprintf(stderr, "지원하지 않는 작업입니다.\n");
    return 2;
}

static int run_benchmark(void) {
    int choice;
    printf("\nBenchmark 알고리즘을 선택하세요.\n"
           "  1. ML-KEM (Kyber1024)\n  2. ML-DSA (cuDilithium)\n");
    choice = read_choice("선택: ");
    if (choice == 1) return run_kyber_benchmark();
    if (choice == 2) return run_dilithium_benchmark();
    fprintf(stderr, "지원하지 않는 알고리즘입니다.\n");
    return 2;
}

int main(int argc, char **argv) {
    int algorithm;
    if (argc == 2) {
        int mode = atoi(argv[1]);
        if (mode != 2 && mode != 3 && mode != 5) {
            fprintf(stderr, "Usage: %s [2|3|5]\n", argv[0]);
            return 2;
        }
        return pqcuda_hybrid_test_mode((pqcuda_dilithium_mode)mode);
    }
    if (argc != 1) {
        fprintf(stderr, "Usage: %s [2|3|5]\n", argv[0]);
        return 2;
    }
    printf("PQCUDA 메뉴를 선택하세요.\n"
           "  1. Run algorithm\n  2. Benchmark\n  3. KAT test\n");
    algorithm = read_choice("선택: ");
    if (algorithm == 1) {
        int selected;
        printf("\n알고리즘을 선택하세요.\n"
               "  1. ML-KEM (Kyber1024)\n  2. ML-DSA (cuDilithium)\n");
        selected = read_choice("선택: ");
        if (selected == 1) return run_kyber();
        if (selected == 2) return run_dilithium();
    }
    if (algorithm == 2) return run_benchmark();
    if (algorithm == 3) {
        printf("아직 준비중입니다.\n");
        return 0;
    }
    fprintf(stderr, "지원하지 않는 메뉴입니다.\n");
    return 2;
}
