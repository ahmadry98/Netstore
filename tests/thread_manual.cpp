#include <iostream>
#include <thread>

void worker()
{
    std::cout << "Hello from the worker\n";
}

int main()
{
    std::thread thread{worker};

    std::cout << "Main is running too\n";

    thread.join();

    std::cout << "Worker finished\n";
}
