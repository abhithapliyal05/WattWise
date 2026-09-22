// matmul.cpp — dense N×N matrix multiply on flattened global arrays.
// Hotspot: innermost k-loop; i*N and the row base are loop-invariant.
#include <iostream>
using namespace std;

const int N = 24;
int A[576];
int B[576];
int C[576];

int main() {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            A[i * N + j] = (i + j) % 7;
            B[i * N + j] = (i * 3 + j) % 5;
        }
    }
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            int sum = 0;
            for (int k = 0; k < N; k++) {
                sum = sum + A[i * N + k] * B[k * N + j];
            }
            C[i * N + j] = sum;
        }
    }
    int trace = 0;
    for (int i = 0; i < N; i++) trace += C[i * N + i];
    cout << "trace " << trace << " corner " << C[N * N - 1] << endl;
    return 0;
}
