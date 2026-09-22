#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pqcuda.h"
#include "benchmark_policy.h"
#include "benchmark_gpu.h"
#include "benchmark_profile.h"
#include "cli_platform.h"

typedef struct {
    double median_latency_ms;
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

static int write_hex_batch_file(const char *path, const uint8_t *data,
                                size_t item_size, size_t batch) {
    size_t item, i;
    int ok;
    FILE *output = fopen(path, "w");
    if (output == NULL) return 0;
    for (item = 0; item < batch; ++item) {
        for (i = 0; i < item_size; ++i)
            fprintf(output, "%02x", (unsigned int)data[item * item_size + i]);
        fputc('\n', output);
    }
    ok = !ferror(output);
    if (fclose(output) != 0) ok = 0;
    return ok;
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
    ptrdiff_t line_length;
    int ok = 0;
    size_t i;

    printf("%s를 입력하세요 (%zu bytes, %zu hex characters).\n"
           "여러 줄로 입력하거나 @파일명으로 불러올 수 있습니다:\n",
           label, size, expected);
    line_length = pqcuda_read_line(&line, &line_capacity, stdin);
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
            line_length = pqcuda_read_line(&line, &line_capacity, stdin);
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
    ptrdiff_t length;
    fputs("메시지를 입력하세요: ", stdout);
    fflush(stdout);
    length = pqcuda_read_line(&line, &capacity, stdin);
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

static int read_batch_size(size_t maximum, size_t *batch) {
    int requested;
    printf("Batch 크기를 입력하세요 (1-%zu).\n", maximum);
    requested = read_choice("Batch: ");
    if (requested < 1 || (size_t)requested > maximum) {
        fprintf(stderr, "Batch는 1부터 %zu 사이여야 합니다.\n", maximum);
        return 0;
    }
    *batch = (size_t)requested;
    return 1;
}

static void profile_path(char *path, size_t capacity, int algorithm, int mode, int op) {
    snprintf(path, capacity, "pqcuda_%s_%d_op%d.profile",
             algorithm == 1 ? "kyber" : "dilithium", mode, op);
}

static int save_benchmark_profile(int algorithm, int mode, int op, size_t batch,
                                  const size_t *values, size_t count) {
    char path[128];
    pqcuda_benchmark_profile profile = {0};
    if (count > PQCUDA_PROFILE_CAPACITY) return 0;
    profile.batch = batch;
    profile.count = count;
    if (count) memcpy(profile.values, values, count * sizeof(*values));
    profile_path(path, sizeof(path), algorithm, mode, op);
    if (!pqcuda_profile_save(path, &profile)) {
        fprintf(stderr, "Cannot save benchmark profile: %s\n", path);
        return 0;
    }
    printf("Updated optimal profile: %s (batch=%zu)\n이번 benchmark 결과로 저장된 최적 설정을 갱신했습니다.\nRun algorithm에서 저장된 benchmark 설정을 선택할 수 있습니다.\n", path, batch);
    return 1;
}

static int read_execution_batch(int algorithm, int mode, int op, size_t maximum,
                                 size_t *batch) {
    char path[128];
    pqcuda_benchmark_profile profile;
    size_t count = algorithm == 1 ? pqcuda_kyber1024_tuned_kernel_count() :
        (op == 2 ? pqcuda_dilithium_tuned_stage_count((pqcuda_dilithium_mode)mode) : 0);
    int choice;
    puts("실행 설정: 1. Batch 직접 입력  2. 저장된 benchmark 최적 설정 사용");
    choice = read_choice("선택: ");
    if (choice == 1) return read_batch_size(maximum, batch);
    if (choice != 2) return 0;
    profile_path(path, sizeof(path), algorithm, mode, op);
    if (!pqcuda_profile_load(path, maximum, count, &profile)) {
        fprintf(stderr, "저장된 설정을 읽을 수 없습니다. 해당 작업의 benchmark를 먼저 실행하세요: %s\n", path);
        return 0;
    }
    if ((algorithm == 1 && pqcuda_kyber1024_apply_launch_profile(profile.values, count) != 0) ||
        (algorithm == 2 && op == 2 && pqcuda_dilithium_apply_launch_profile(
            (pqcuda_dilithium_mode)mode, profile.values, count) != 0)) {
        fprintf(stderr, "저장된 커널 설정을 적용할 수 없습니다. benchmark를 다시 실행하세요.\n");
        return 0;
    }
    *batch = profile.batch;
    printf("Applied benchmark profile: %s, batch=%zu\n", path, *batch);
    return 1;
}

static int read_message_batch(size_t batch, uint8_t **messages,
                              size_t *message_length) {
    uint8_t *message = NULL;
    size_t total, item;
    printf("입력한 메시지를 배치 %zu개 항목에 공통으로 사용합니다.\n", batch);
    if (!read_message(&message, message_length)) return 0;
    if (*message_length > SIZE_MAX / batch) {
        free(message);
        return 0;
    }
    total = *message_length * batch;
    *messages = (uint8_t *)malloc(total > 0 ? total : 1);
    if (*messages == NULL) {
        free(message);
        return 0;
    }
    if (*message_length > 0) {
        for (item = 0; item < batch; ++item)
            memcpy(*messages + item * *message_length, message, *message_length);
    }
    free(message);
    return 1;
}

static int compare_double(const void *left, const void *right) {
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}

static benchmark_stats summarize(double *samples, size_t sample_count,
                                 size_t operations) {
    benchmark_stats stats;
    size_t i;
    for (i = 0; i < sample_count; ++i) samples[i] /= (double)operations;
    qsort(samples, sample_count, sizeof(*samples), compare_double);
    stats.median_latency_ms = (samples[(sample_count - 1) / 2] + samples[sample_count / 2]) / 2.0;
    stats.throughput = stats.median_latency_ms > 0.0
        ? 1000.0 / stats.median_latency_ms : 0.0;
    return stats;
}

static size_t print_auto_benchmark_results(const char *algorithm, const char *name,
                                           const benchmark_result *results,
                                           size_t result_count) {
    size_t i, best = 0;
    double maximum = 0.0, best_total = 0.0;
    for (i = 0; i < result_count; ++i)
        if (results[i].stats.throughput > maximum) maximum = results[i].stats.throughput;
    printf("\n%s %s automatic search results\n", algorithm, name);
    puts("Batch size | Median total (ms) | Latency (ms/op) | Throughput (ops/s)");
    for (i = 0; i < result_count; ++i) {
        double total = results[i].stats.median_latency_ms * results[i].batch;
        printf("%10zu | %17.6f | %14.6f | %18.3f\n", results[i].batch,
               total, results[i].stats.median_latency_ms, results[i].stats.throughput);
        if (pqcuda_benchmark_prefer(results[i].stats.throughput, total, maximum, best_total)) {
            best = i;
            best_total = total;
        }
    }
    printf("\nRecommended: work items=%zu, median total=%.6f ms, throughput=%.3f ops/s "
           "(%.2f%% of measured peak %.3f ops/s)\n", results[best].batch, best_total,
           results[best].stats.throughput, 100.0 * results[best].stats.throughput / maximum,
           maximum);
    return best;
}

static int run_kyber_benchmark(void) {
    static const char *names[] = {"", "keypair", "encap", "decap"};
    uint8_t *pk = NULL;
    uint8_t *sk = NULL;
    uint8_t *ct = NULL;
    uint8_t *ss = NULL;
    enum { MAX_WORKLOADS = 64, MAX_KERNELS = 32 };
    benchmark_result results[MAX_WORKLOADS];
    size_t blocks[MAX_WORKLOADS][MAX_KERNELS];
    size_t workloads[MAX_WORKLOADS];
    size_t count, candidate, completed = 0, best = 0, kernel;
    size_t kernel_count = pqcuda_kyber1024_tuned_kernel_count();
    double maximum_throughput = 0.0, best_total_ms = 0.0;
    double samples[PQCUDA_BENCHMARK_SAMPLE_RUNS];
    const size_t maximum_batch = pqcuda_kyber1024_max_batch_size();
    size_t sample_count = 0;
    size_t batch;
    int run;
    int selected;
    int return_code = 1;

    printf("\nML-KEM Benchmark 작업을 선택하세요.\n"
           "  1. keypair\n  2. encap\n  3. decap\n");
    selected = read_choice("선택: ");
    if (selected < 1 || selected > 3) return 2;

    count = pqcuda_benchmark_workloads(maximum_batch, workloads, MAX_WORKLOADS);
    if (!count || kernel_count > MAX_KERNELS) return 1;
    puts("Automatic search: kernel block sizes 16,32,64,128,192,256,512,1024;");
    puts("grid = ceil(work items / block). Unsupported block sizes are excluded.");
    puts("Recommendation: lowest total latency at >=95% of measured peak throughput.");
    puts("API timing includes allocation, transfers, CPU work and GPU execution; excludes tuning.");
    for (candidate = 0; candidate < count; ++candidate) {
        batch = workloads[candidate];
        sample_count = 0;
        pk = (uint8_t *)malloc(batch * PQCUDA_KYBER1024_PUBLIC_KEY_BYTES);
        sk = (uint8_t *)malloc(batch * PQCUDA_KYBER1024_SECRET_KEY_BYTES);
        ct = (uint8_t *)malloc(batch * PQCUDA_KYBER1024_CIPHERTEXT_BYTES);
        ss = (uint8_t *)malloc(batch * PQCUDA_KYBER1024_SHARED_SECRET_BYTES);
        if (pk == NULL || sk == NULL || ct == NULL || ss == NULL) {
            fprintf(stderr, "Batch %zu용 호스트 메모리 할당에 실패했습니다.\n",
                    batch);
            goto cleanup;
        }

        printf("\nTuning Kyber1024 kernels for batch=%zu...\n", batch);
        fflush(stdout);
        if (pqcuda_kyber1024_tune_launch_profile(batch) != 0) goto cleanup;

        if (selected != 1 &&
            pqcuda_kyber1024_keypair_batch(pk, sk, batch) != 0)
            goto cleanup;
        if (selected == 3 &&
            pqcuda_kyber1024_encapsulate_batch(ct, ss, pk, batch) != 0)
            goto cleanup;

        for (run = 0;
             run < PQCUDA_BENCHMARK_WARMUP_RUNS + PQCUDA_BENCHMARK_SAMPLE_RUNS;
             ++run) {
            pqcuda_timestamp start, end;
            int call_result = 0;
            if (!pqcuda_monotonic_now(&start)) goto cleanup;
            if (selected == 1)
                call_result = pqcuda_kyber1024_keypair_batch(
                    pk, sk, batch);
            else if (selected == 2)
                call_result = pqcuda_kyber1024_encapsulate_batch(
                    ct, ss, pk, batch);
            else
                call_result = pqcuda_kyber1024_decapsulate_batch(
                    ss, ct, sk, batch);
            if (!pqcuda_monotonic_now(&end)) goto cleanup;
            if (call_result != 0) goto cleanup;
            if (run >= PQCUDA_BENCHMARK_WARMUP_RUNS) {
                double duration = pqcuda_elapsed_ms(start, end);
                if (duration < 0.0) goto cleanup;
                samples[sample_count++] = duration;
            }
        }
        results[completed].batch = batch;
        results[completed].stats = summarize(samples, sample_count, batch);
        for (kernel = 0; kernel < kernel_count; ++kernel)
            blocks[completed][kernel] = pqcuda_kyber1024_tuned_kernel_threads(kernel);
        if (results[completed].stats.throughput > maximum_throughput)
            maximum_throughput = results[completed].stats.throughput;
        printf("Work items=%zu: total=%.6f ms, throughput=%.3f ops/s\n", batch,
               results[completed].stats.median_latency_ms * batch,
               results[completed].stats.throughput);
        fflush(stdout);
        ++completed;
        free(pk); free(sk); free(ct); free(ss);
        pk = NULL; sk = NULL; ct = NULL; ss = NULL;
    }
    printf("\nML-KEM %s automatic search results\n", names[selected]);
    puts("Batch size | Median total (ms) | Latency (ms/op) | Throughput (ops/s)");
    for (candidate = 0; candidate < completed; ++candidate) {
        double total_ms = results[candidate].stats.median_latency_ms * results[candidate].batch;
        printf("%10zu | %17.6f | %14.6f | %18.3f\n",
               results[candidate].batch, total_ms,
               results[candidate].stats.median_latency_ms, results[candidate].stats.throughput);
        if (pqcuda_benchmark_prefer(results[candidate].stats.throughput, total_ms,
                                    maximum_throughput, best_total_ms)) {
            best = candidate;
            best_total_ms = total_ms;
        }
    }
    batch = results[best].batch;
    printf("\nRecommended: work items=%zu, median total=%.6f ms, throughput=%.3f ops/s "
           "(%.2f%% of measured peak %.3f ops/s)\n", batch, best_total_ms,
           results[best].stats.throughput,
           100.0 * results[best].stats.throughput / maximum_throughput, maximum_throughput);
    puts("Kernel launch profile (1D grid and block):");
    for (kernel = 0; kernel < kernel_count; ++kernel) {
        size_t block = blocks[best][kernel];
        printf("  %-30s grid=%zu, block=%zu\n",
               pqcuda_kyber1024_tuned_kernel_name(kernel),
               (batch + block - 1) / block, block);
    }
    puts("Profile lists all CPA pipeline kernels; each operation launches its own subset.");
    if (pqcuda_kyber1024_apply_launch_profile(blocks[best], kernel_count) != 0 ||
        !save_benchmark_profile(1, 1024, selected, batch, blocks[best], kernel_count)) goto cleanup;
    return_code = 0;

cleanup:
    if (return_code != 0) fprintf(stderr, "Automatic benchmark failed at work items=%zu; no recommendation produced.\n", batch);
    free(pk);
    free(sk);
    free(ct);
    free(ss);
    return return_code;
}

static int run_dilithium_benchmark(void) {
    static const uint8_t message_pattern[] = {'P', 'Q', 'C', 'U', 'D', 'A'};
    static const char *names[] = {"", "keypair", "sign", "verify"};
    enum { MAX_WORKLOADS = 64, MAX_STAGES = 8 };
    benchmark_result results[MAX_WORKLOADS];
    pqcuda_launch_config profiles[MAX_WORKLOADS][MAX_STAGES];
    size_t variants[MAX_WORKLOADS][MAX_STAGES];
    size_t workloads[MAX_WORKLOADS];
    size_t count, candidate, best, stage_count;
    size_t pk_item_size, sk_item_size, sig_item_size;
    size_t pk_size, sk_size, sig_size, batch = 0;
    uint8_t *pk = NULL, *sk = NULL, *sig = NULL, *messages = NULL;
    size_t sig_length = 0, i;
    double samples[PQCUDA_BENCHMARK_SAMPLE_RUNS];
    int run, selected, operation, return_code = 1;
    pqcuda_dilithium_mode mode;
    char algorithm_name[32];

    printf("\nML-DSA mode를 선택하세요.\n  2. Dilithium-2\n"
           "  3. Dilithium-3\n  5. Dilithium-5\n");
    selected = read_choice("선택: ");
    if (selected != 2 && selected != 3 && selected != 5) return 2;
    mode = (pqcuda_dilithium_mode)selected;
    printf("\nBenchmark 작업을 선택하세요.\n"
           "  1. keypair\n  2. sign\n  3. verify\n");
    operation = read_choice("선택: ");
    if (operation < 1 || operation > 3) return 2;

    count = pqcuda_dilithium_benchmark_workloads(
        pqcuda_dilithium_max_batch_size(), workloads, MAX_WORKLOADS);
    stage_count = pqcuda_dilithium_tuned_stage_count(mode);
    if (!count || !stage_count || stage_count > MAX_STAGES) return 1;
    puts("Automatic search: SM-aligned batch steps (~1000-2000 items) and supported kernel variants.");
    puts("Recommendation: lowest total latency at >=95% of measured peak throughput.");
    puts("API timing includes allocation, transfers, CPU work and GPU execution; excludes tuning.");
    if (operation == 2)
        puts("Sign tunes each stage at each work count; rejection-stage grids use internal execution capacity.");
    else
        puts("Keypair/verify use block=(32,1), grid=work items; no sign-variant tuning is needed.");

    pk_item_size = pqcuda_dilithium_public_key_bytes(mode);
    sk_item_size = pqcuda_dilithium_secret_key_bytes(mode);
    sig_item_size = pqcuda_dilithium_signature_bytes(mode);
    for (candidate = 0; candidate < count; ++candidate) {
        size_t sample_count = 0;
        batch = workloads[candidate];
        sig_length = 0;
        printf("\nML-DSA-%d %s: work items=%zu (%zu/%zu)%s\n",
               selected, names[operation], batch, candidate + 1, count,
               operation == 2 ? "; tuning kernel variants..." : "");
        fflush(stdout);
        pk_size = pk_item_size * batch;
        sk_size = sk_item_size * batch;
        sig_size = sig_item_size * batch;
        pk = (uint8_t *)malloc(pk_size);
        sk = (uint8_t *)malloc(sk_size);
        sig = (uint8_t *)malloc(sig_size);
        messages = (uint8_t *)malloc(sizeof(message_pattern) * batch);
        if (!pk || !sk || !sig || !messages) goto cleanup;
        for (i = 0; i < batch; ++i)
            memcpy(messages + i * sizeof(message_pattern), message_pattern, sizeof(message_pattern));

        if (operation == 2) {
            if (pqcuda_dilithium_tune_sign_kernels(mode, batch) != 0) goto cleanup;
            for (i = 0; i < stage_count; ++i)
                if (pqcuda_dilithium_tuned_launch_config(mode, i, batch,
                                                       &profiles[candidate][i]) != 0) goto cleanup;
        }
        if (operation != 1 &&
            pqcuda_dilithium_keypair_batch(mode, pk, pk_size, sk, sk_size, batch) != 0) goto cleanup;
        if (operation == 3 &&
            pqcuda_dilithium_sign_batch(mode, sig, sig_size, &sig_length, messages,
                sizeof(message_pattern), sk, sk_size, batch) != 0) goto cleanup;

        for (run = 0; run < PQCUDA_BENCHMARK_WARMUP_RUNS + PQCUDA_BENCHMARK_SAMPLE_RUNS; ++run) {
            pqcuda_timestamp start, end;
            int call_result;
            if (!pqcuda_monotonic_now(&start)) goto cleanup;
            if (operation == 1)
                call_result = pqcuda_dilithium_keypair_batch(mode, pk, pk_size, sk, sk_size, batch);
            else if (operation == 2)
                call_result = pqcuda_dilithium_sign_batch(mode, sig, sig_size, &sig_length,
                    messages, sizeof(message_pattern), sk, sk_size, batch);
            else
                call_result = pqcuda_dilithium_verify_batch(mode, sig, sig_length,
                    messages, sizeof(message_pattern), pk, pk_size, batch);
            if (!pqcuda_monotonic_now(&end) || call_result != 0) goto cleanup;
            if (run >= PQCUDA_BENCHMARK_WARMUP_RUNS) {
                double duration = pqcuda_elapsed_ms(start, end);
                if (duration <= 0.0) goto cleanup;
                samples[sample_count++] = duration;
            }
        }
        /* Validate the final generated keys/signatures outside the timed region. */
        if (operation == 1 &&
            pqcuda_dilithium_sign_batch(mode, sig, sig_size, &sig_length, messages,
                sizeof(message_pattern), sk, sk_size, batch) != 0) goto cleanup;
        if (sig_length != sig_item_size ||
            pqcuda_dilithium_verify_batch(mode, sig, sig_length, messages,
                sizeof(message_pattern), pk, pk_size, batch) != 0) goto cleanup;
        if (operation == 2 && pqcuda_dilithium_get_launch_profile(
                mode, variants[candidate], stage_count) != 0) goto cleanup;
        results[candidate].batch = batch;
        results[candidate].stats = summarize(samples, sample_count, batch);
        printf("Work items=%zu: total=%.6f ms, throughput=%.3f ops/s\n", batch,
               results[candidate].stats.median_latency_ms * batch,
               results[candidate].stats.throughput);
        fflush(stdout);
        free(pk); free(sk); free(sig); free(messages);
        pk = NULL; sk = NULL; sig = NULL; messages = NULL;
    }
    snprintf(algorithm_name, sizeof(algorithm_name), "ML-DSA-%d", selected);
    best = print_auto_benchmark_results(algorithm_name, names[operation], results, count);
    batch = results[best].batch;
    puts("Recommended kernel launch profile (grid.x, block.x, block.y):");
    if (operation == 2) {
        for (i = 0; i < stage_count; ++i) {
            const pqcuda_launch_config *config = &profiles[best][i];
            printf("  %-16s %-40s grid=%zu, block=(%zu,%zu)\n",
                   pqcuda_dilithium_tuned_stage_name(mode, i), config->kernel_name,
                   config->grid_x, config->block_x, config->block_y);
        }
        puts("  sig_copy_kernel: grid=copy_count (runtime), block=(96,1).");
        puts("Rejection-stage grids are per round; the number of rounds depends on signature acceptance.");
    } else {
        printf("  %s: grid=%zu, block=(32,1)\n",
               operation == 1 ? "gpu_keypair" : "gpu_verify", batch);
    }
    if (operation == 2 && pqcuda_dilithium_apply_launch_profile(
            mode, variants[best], stage_count) != 0) goto cleanup;
    if (!save_benchmark_profile(2, selected, operation, batch,
            operation == 2 ? variants[best] : NULL, operation == 2 ? stage_count : 0)) goto cleanup;
    return_code = 0;
cleanup:
    if (return_code != 0)
        fprintf(stderr, "Automatic benchmark failed at work items=%zu; no recommendation produced.\n", batch);
    free(pk); free(sk); free(sig); free(messages);
    return return_code;
}

static int run_kyber(void) {
    int op, result = 1;
    size_t batch, pk_size, sk_size, ct_size, ss_size;
    uint8_t *pk = NULL, *sk = NULL, *ct = NULL, *ss = NULL;
    printf("\n보안 레벨을 선택하세요.\n  1. Kyber1024\n");
    if (read_choice("선택: ") != 1) {
        fprintf(stderr, "지원하지 않는 보안 레벨입니다.\n");
        return 2;
    }
    printf("\n작업을 선택하세요.\n  1. keypair\n  2. encaps\n  3. decaps\n");
    op = read_choice("선택: ");
    if (op < 1 || op > 3) {
        fprintf(stderr, "지원하지 않는 작업입니다.\n");
        return 2;
    }
    if (!read_execution_batch(1, 1024, op, pqcuda_kyber1024_max_batch_size(), &batch)) return 2;
    pk_size = batch * PQCUDA_KYBER1024_PUBLIC_KEY_BYTES;
    sk_size = batch * PQCUDA_KYBER1024_SECRET_KEY_BYTES;
    ct_size = batch * PQCUDA_KYBER1024_CIPHERTEXT_BYTES;
    ss_size = batch * PQCUDA_KYBER1024_SHARED_SECRET_BYTES;

    if (op == 1) {
        pk = (uint8_t *)malloc(pk_size);
        sk = (uint8_t *)malloc(sk_size);
        if (pk == NULL || sk == NULL) goto done;
        if (pqcuda_kyber1024_keypair_batch(pk, sk, batch) != 0) goto done;
        if (!write_hex_batch_file("kyber1024_public_key.hex", pk,
                                  PQCUDA_KYBER1024_PUBLIC_KEY_BYTES, batch) ||
            !write_hex_batch_file("kyber1024_private_key.hex", sk,
                                  PQCUDA_KYBER1024_SECRET_KEY_BYTES, batch)) goto done;
        printf("Public keys (%zu bytes): kyber1024_public_key.hex\n"
               "Private keys (%zu bytes): kyber1024_private_key.hex\n",
               pk_size, sk_size);
    } else if (op == 2) {
        if (!read_hex("Public keys (배치 순서대로)", pk_size, &pk)) {
            result = 2;
            goto done;
        }
        ct = (uint8_t *)malloc(ct_size);
        ss = (uint8_t *)malloc(ss_size);
        if (ct == NULL || ss == NULL) goto done;
        if (pqcuda_kyber1024_encapsulate_batch(ct, ss, pk, batch) != 0) goto done;
        if (!write_hex_batch_file("kyber1024_ciphertext.hex", ct,
                                  PQCUDA_KYBER1024_CIPHERTEXT_BYTES, batch) ||
            !write_hex_batch_file("kyber1024_shared_key_encap.hex", ss,
                                  PQCUDA_KYBER1024_SHARED_SECRET_BYTES, batch)) goto done;
        printf("Ciphertexts (%zu bytes): kyber1024_ciphertext.hex\n"
               "Shared secrets (%zu bytes): kyber1024_shared_key_encap.hex\n",
               ct_size, ss_size);
    } else {
        if (!read_hex("Ciphertexts (배치 순서대로)", ct_size, &ct) ||
            !read_hex("Private keys (배치 순서대로)", sk_size, &sk)) {
            result = 2;
            goto done;
        }
        ss = (uint8_t *)malloc(ss_size);
        if (ss == NULL) goto done;
        if (pqcuda_kyber1024_decapsulate_batch(ss, ct, sk, batch) != 0) goto done;
        if (!write_hex_batch_file("kyber1024_shared_key_decap.hex", ss,
                                  PQCUDA_KYBER1024_SHARED_SECRET_BYTES, batch)) goto done;
        if (batch == 1) print_hex("Shared secret", ss, ss_size);
        printf("Shared secrets (%zu bytes): kyber1024_shared_key_decap.hex\n", ss_size);
    }
    printf("Batch %zu 완료. 각 파일은 항목당 한 줄의 hex 형식입니다.\n", batch);
    result = 0;
done:
    if (result == 1) fprintf(stderr, "메모리 할당, 알고리즘 실행 또는 결과 파일 저장에 실패했습니다.\n");
    free(pk); free(sk); free(ct); free(ss);
    return result;
}

static int run_dilithium(void) {
    int selected, op, result = 1;
    pqcuda_dilithium_mode mode;
    size_t batch, pk_item_size, sk_item_size, sig_item_size;
    size_t pk_size, sk_size, sig_size, message_length = 0, sig_length = 0;
    uint8_t *pk = NULL, *sk = NULL, *sig = NULL, *messages = NULL;
    char public_key_file[256], private_key_file[256], signature_file[256];
    printf("\nML-DSA 보안 레벨(mode)을 선택하세요.\n"
           "  2. Dilithium-2\n  3. Dilithium-3\n  5. Dilithium-5\n");
    selected = read_choice("선택: ");
    if (selected != 2 && selected != 3 && selected != 5) {
        fprintf(stderr, "지원하지 않는 mode입니다.\n");
        return 2;
    }
    mode = (pqcuda_dilithium_mode)selected;
    printf("\n작업을 선택하세요.\n  1. keypair\n  2. sign\n  3. verify\n");
    op = read_choice("선택: ");
    if (op < 1 || op > 3) {
        fprintf(stderr, "지원하지 않는 작업입니다.\n");
        return 2;
    }
    if (!read_execution_batch(2, selected, op, pqcuda_dilithium_max_batch_size(), &batch)) return 2;
    pk_item_size = pqcuda_dilithium_public_key_bytes(mode);
    sk_item_size = pqcuda_dilithium_secret_key_bytes(mode);
    sig_item_size = pqcuda_dilithium_signature_bytes(mode);
    pk_size = pk_item_size * batch;
    sk_size = sk_item_size * batch;
    sig_size = sig_item_size * batch;
    snprintf(public_key_file, sizeof(public_key_file),
             "cudilithium%d_public_key.hex", selected);
    snprintf(private_key_file, sizeof(private_key_file),
             "cudilithium%d_private_key.hex", selected);
    snprintf(signature_file, sizeof(signature_file),
             "cudilithium%d_signature.hex", selected);

    if (op == 1) {
        pk = (uint8_t *)malloc(pk_size);
        sk = (uint8_t *)malloc(sk_size);
        if (pk == NULL || sk == NULL) goto done;
        if (pqcuda_dilithium_keypair_batch(mode, pk, pk_size, sk, sk_size, batch) != 0) goto done;
        if (!write_hex_batch_file(public_key_file, pk, pk_item_size, batch) ||
            !write_hex_batch_file(private_key_file, sk, sk_item_size, batch)) goto done;
        printf("Public keys (%zu bytes): %s\nPrivate keys (%zu bytes): %s\n",
               pk_size, public_key_file, sk_size, private_key_file);
    } else if (op == 2) {
        if (!read_message_batch(batch, &messages, &message_length) ||
            !read_hex("Private keys (배치 순서대로)", sk_size, &sk)) {
            result = 2;
            goto done;
        }
        sig = (uint8_t *)malloc(sig_size);
        if (sig == NULL) goto done;
        if (pqcuda_dilithium_sign_batch(mode, sig, sig_size, &sig_length,
                messages, message_length, sk, sk_size, batch) != 0 ||
            sig_length != sig_item_size) goto done;
        if (!write_hex_batch_file(signature_file, sig, sig_item_size, batch)) goto done;
        printf("Signatures (%zu bytes): %s\n", sig_size, signature_file);
    } else {
        if (!read_message_batch(batch, &messages, &message_length) ||
            !read_hex("Public keys (배치 순서대로)", pk_size, &pk) ||
            !read_hex("Signatures (배치 순서대로)", sig_size, &sig)) {
            result = 2;
            goto done;
        }
        result = pqcuda_dilithium_verify_batch(mode, sig, sig_item_size,
                    messages, message_length, pk, pk_size, batch) == 0 ? 0 : 1;
        printf("Batch %zu: %s\n", batch,
               result == 0 ? "All signatures: VALID" : "Signature verification: INVALID or failed");
        free(messages); free(pk); free(sig);
        return result;
    }
    printf("Batch %zu 완료. 각 파일은 항목당 한 줄의 hex 형식입니다.\n", batch);
    result = 0;
done:
    if (result == 1) fprintf(stderr, "메모리 할당, 알고리즘 실행 또는 결과 파일 저장에 실패했습니다.\n");
    free(messages); free(pk); free(sk); free(sig);
    return result;
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
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc != 1) {
        fprintf(stderr, "Usage: %s\n", argv[0]);
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
