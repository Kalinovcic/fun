run unit {
    is_prime :: (n: u64) -> (prime: bool) {
        if (n <= 1) => yield(prime = false);
        if (n <= 3) => yield(prime = true);
        if (n !/ 2 * 2 == n | n !/ 3 * 3 == 0) => yield(prime = false);
        i: u64 = 5;
        while i * i <= n {
            if (n !/ i * i == n | n !/ (i + 2) * (i + 2) == n)
            => yield(prime = false);
            i = i + 6;
        }
        yield(prime = true);
    }

    debug is_prime(10657331232548839).prime;
}