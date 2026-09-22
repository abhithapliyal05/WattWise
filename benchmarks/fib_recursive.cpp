// fib_recursive.cpp — call-dominated workload (exponential recursion).
#include <iostream>
using namespace std;

int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int main() {
    int total = 0;
    for (int k = 0; k < 18; k++) total = total + fib(k);
    cout << "fib(20) = " << fib(20) << ", sum fib(0..17) = " << total << endl;
    return 0;
}
