#include <iostream>
#include <string>
using namespace std;

/* Пример корректной программы */

struct Point {
    int x;
    int y;
};

int sum(int a, int b) {
    return a + b;
}

int main() {
    int n;
    cin >> n;

    int total = 0, i = 1;
    double avg = 0.5;
    string name = "Hello, world!";
    char c = '\n';
    int arr[10];

    for (int k = 0; k < n; k++) {
        arr[k] = k * 2;
        total += arr[k];
    }

    while (i < 5) {
        i++;
    }

    Point p;
    p.x = sum(total, i);

    if (total > 10 && avg < 1.0) {
        cout << name << " " << p.x << c;
    } else {
        cout << "small" << endl;
    }
    return 0;
}
