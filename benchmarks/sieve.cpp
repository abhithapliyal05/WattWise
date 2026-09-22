// sieve.cpp — Sieve of Eratosthenes; memory-store dominated.
#include <iostream>
using namespace std;

const int LIMIT = 20000;
int composite[20001];

int main() {
    int count = 0;
    for (int i = 2; i <= LIMIT; i++) {
        if (composite[i] == 0) {
            count++;
            int j = i * i;
            while (j <= LIMIT) {
                composite[j] = 1;
                j += i;
            }
        }
    }
    cout << "primes below " << LIMIT << ": " << count << endl;
    return 0;
}
