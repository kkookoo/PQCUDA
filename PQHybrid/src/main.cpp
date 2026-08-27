#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "pqcuda.h"

namespace {
int read_choice(const char *prompt) {
    std::string line; std::cout << prompt;
    if (!std::getline(std::cin, line)) return -1;
    std::istringstream input(line); int value = -1; char extra;
    return (input >> value) && !(input >> extra) ? value : -1;
}

void print_hex(const char *label, const uint8_t *data, size_t size) {
    std::cout << label << " (" << size << " bytes):\n";
    std::ios old(nullptr); old.copyfmt(std::cout);
    std::cout << std::hex << std::setfill('0');
    for (size_t i = 0; i < size; ++i)
        std::cout << std::setw(2) << static_cast<unsigned int>(data[i]);
    std::cout << '\n'; std::cout.copyfmt(old);
}

bool write_hex_file(const char *path, const uint8_t *data, size_t size) {
    std::ofstream output(path);
    if (!output) return false;
    output << std::hex << std::setfill('0');
    for (size_t i = 0; i < size; ++i)
        output << std::setw(2) << static_cast<unsigned int>(data[i]);
    output << '\n';
    return static_cast<bool>(output);
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

bool read_hex(const char *label, size_t size, std::vector<uint8_t> &out) {
    std::cout << label << "를 입력하세요 (" << size << " bytes, "
              << size * 2 << " hex characters).\n"
              << "여러 줄로 입력하거나 @파일명으로 불러올 수 있습니다:\n";
    std::string line, hex;
    if (!std::getline(std::cin, line)) return false;
    if (!line.empty() && line[0] == '@') {
        std::ifstream input(line.substr(1));
        if (!input) {
            std::cerr << "파일을 열 수 없습니다: " << line.substr(1) << '\n';
            return false;
        }
        char c;
        while (input.get(c))
            if (!std::isspace(static_cast<unsigned char>(c))) hex.push_back(c);
    } else {
        while (true) {
            for (char c : line)
                if (!std::isspace(static_cast<unsigned char>(c))) hex.push_back(c);
            if (hex.size() >= size * 2) break;
            std::cout << "  " << hex.size() << "/" << size * 2
                      << " hex characters 입력됨. 계속 입력하세요:\n";
            if (!std::getline(std::cin, line)) return false;
        }
    }
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X'))
        hex.erase(0, 2);
    if (hex.size() != size * 2) {
        std::cerr << "입력 길이가 올바르지 않습니다.\n"; return false;
    }
    out.resize(size);
    for (size_t i = 0; i < size; ++i) {
        int hi = hex_value(hex[2 * i]), lo = hex_value(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            std::cerr << "hex가 아닌 문자가 포함되어 있습니다.\n"; return false;
        }
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

bool read_message(std::vector<uint8_t> &message) {
    std::cout << "메시지를 입력하세요: "; std::string line;
    if (!std::getline(std::cin, line)) return false;
    message.assign(line.begin(), line.end()); return true;
}

int run_kyber() {
    std::cout << "\n보안 레벨을 선택하세요.\n  1. Kyber1024\n";
    if (read_choice("선택: ") != 1) {
        std::cerr << "지원하지 않는 보안 레벨입니다.\n"; return 2;
    }
    std::cout << "\n작업을 선택하세요.\n  1. keypair\n"
                 "  2. encaps\n  3. decaps\n";
    int op = read_choice("선택: ");
    if (op == 1) {
        std::vector<uint8_t> pk(PQCUDA_KYBER1024_PUBLIC_KEY_BYTES);
        std::vector<uint8_t> sk(PQCUDA_KYBER1024_SECRET_KEY_BYTES);
        if (pqcuda_kyber1024_keypair(pk.data(), sk.data()) != 0) return 1;
        if (!write_hex_file("kyber1024_public_key.hex", pk.data(), pk.size()) ||
            !write_hex_file("kyber1024_private_key.hex", sk.data(), sk.size())) {
            std::cerr << "KeyGen 결과 파일 저장에 실패했습니다.\n";
            return 1;
        }
        std::cout << "Public key (" << pk.size()
                  << " bytes): kyber1024_public_key.hex\n"
                  << "Private key (" << sk.size()
                  << " bytes): kyber1024_private_key.hex\n";
        return 0;
    }
    if (op == 2) {
        std::vector<uint8_t> pk, ct(PQCUDA_KYBER1024_CIPHERTEXT_BYTES);
        std::vector<uint8_t> ss(PQCUDA_KYBER1024_SHARED_SECRET_BYTES);
        if (!read_hex("Public key", PQCUDA_KYBER1024_PUBLIC_KEY_BYTES, pk)) return 2;
        if (pqcuda_kyber1024_encapsulate(ct.data(), ss.data(), pk.data()) != 0) return 1;
        if (write_hex_file("kyber1024_ciphertext.hex", ct.data(), ct.size()) &&
            write_hex_file("kyber1024_shared_key_encap.hex",
                           ss.data(), ss.size()))
            std::cout << "Ciphertext (" << ct.size()
                      << " bytes): kyber1024_ciphertext.hex\n"
                      << "Shared secret (" << ss.size()
                      << " bytes): kyber1024_shared_key_encap.hex\n";
        else
            std::cerr << "Encapsulation 결과 파일 저장에 실패했습니다.\n";
        return 0;
    }
    if (op == 3) {
        std::vector<uint8_t> ct, sk, ss(PQCUDA_KYBER1024_SHARED_SECRET_BYTES);
        if (!read_hex("Ciphertext", PQCUDA_KYBER1024_CIPHERTEXT_BYTES, ct) ||
            !read_hex("Private key", PQCUDA_KYBER1024_SECRET_KEY_BYTES, sk)) return 2;
        if (pqcuda_kyber1024_decapsulate(ss.data(), ct.data(), sk.data()) != 0) return 1;
        print_hex("Shared secret", ss.data(), ss.size());
        if (write_hex_file("kyber1024_shared_key_decap.hex",
                           ss.data(), ss.size()))
            std::cout << "결과를 kyber1024_shared_key_decap.hex에 "
                         "저장했습니다.\n";
        else
            std::cerr << "Decapsulation 결과 파일 저장에 실패했습니다.\n";
        return 0;
    }
    std::cerr << "지원하지 않는 작업입니다.\n"; return 2;
}

int run_dilithium() {
    std::cout << "\nML-DSA 보안 레벨(mode)을 선택하세요.\n"
                 "  2. Dilithium-2\n  3. Dilithium-3\n  5. Dilithium-5\n";
    int selected = read_choice("선택: ");
    if (selected != 2 && selected != 3 && selected != 5) {
        std::cerr << "지원하지 않는 mode입니다.\n"; return 2;
    }
    auto mode = static_cast<pqcuda_dilithium_mode>(selected);
    size_t pk_size = pqcuda_dilithium_public_key_bytes(mode);
    size_t sk_size = pqcuda_dilithium_secret_key_bytes(mode);
    size_t sig_size = pqcuda_dilithium_signature_bytes(mode);
    const std::string prefix = "cudilithium" + std::to_string(selected);
    const std::string public_key_file = prefix + "_public_key.hex";
    const std::string private_key_file = prefix + "_private_key.hex";
    const std::string signature_file = prefix + "_signature.hex";
    std::cout << "\n작업을 선택하세요.\n  1. keypair\n"
                 "  2. sign\n  3. verify\n";
    int op = read_choice("선택: ");
    if (op == 1) {
        std::vector<uint8_t> pk(pk_size), sk(sk_size);
        if (pqcuda_dilithium_keypair(mode, pk.data(), pk.size(),
                                     sk.data(), sk.size()) != 0) return 1;
        if (!write_hex_file(public_key_file.c_str(), pk.data(), pk.size()) ||
            !write_hex_file(private_key_file.c_str(), sk.data(), sk.size())) {
            std::cerr << "KeyGen 결과 파일 저장에 실패했습니다.\n";
            return 1;
        }
        std::cout << "Public key (" << pk.size() << " bytes): "
                  << public_key_file << '\n'
                  << "Private key (" << sk.size() << " bytes): "
                  << private_key_file << '\n';
        return 0;
    }
    if (op == 2) {
        std::vector<uint8_t> message, sk, sig(sig_size);
        if (!read_message(message) || !read_hex("Private key", sk_size, sk)) return 2;
        size_t sig_length = 0;
        if (pqcuda_dilithium_sign(mode, sig.data(), sig.size(), &sig_length,
                                  message.data(), message.size(), sk.data(), sk.size()) != 0)
            return 1;
        if (!write_hex_file(signature_file.c_str(), sig.data(), sig_length)) {
            std::cerr << "Signature 파일 저장에 실패했습니다.\n";
            return 1;
        }
        std::cout << "Signature (" << sig_length << " bytes): "
                  << signature_file << '\n';
        return 0;
    }
    if (op == 3) {
        std::vector<uint8_t> message, pk, sig;
        if (!read_message(message) || !read_hex("Public key", pk_size, pk) ||
            !read_hex("Signature", sig_size, sig)) return 2;
        int result = pqcuda_dilithium_verify(mode, sig.data(), sig.size(),
                                             message.data(), message.size(),
                                             pk.data(), pk.size());
        std::cout << (result == 0 ? "Signature: VALID\n" : "Signature: INVALID\n");
        return result == 0 ? 0 : 1;
    }
    std::cerr << "지원하지 않는 작업입니다.\n"; return 2;
}
} // namespace

int main(int argc, char **argv) {
    if (argc == 2) { // Backward-compatible non-interactive hybrid test.
        int mode = std::atoi(argv[1]);
        if (mode != 2 && mode != 3 && mode != 5) {
            std::fprintf(stderr, "Usage: %s [2|3|5]\n", argv[0]); return 2;
        }
        return pqcuda_hybrid_test_mode(static_cast<pqcuda_dilithium_mode>(mode));
    }
    if (argc != 1) { std::fprintf(stderr, "Usage: %s [2|3|5]\n", argv[0]); return 2; }
    std::cout << "PQCUDA 알고리즘을 선택하세요.\n"
                 "  1. ML-KEM (Kyber1024)\n  2. ML-DSA (cuDilithium)\n";
    int algorithm = read_choice("선택: ");
    if (algorithm == 1) return run_kyber();
    if (algorithm == 2) return run_dilithium();
    std::cerr << "지원하지 않는 알고리즘입니다.\n"; return 2;
}
