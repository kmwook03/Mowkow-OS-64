# M9d double-float smoke. Keep this usable by both x86_64 and AArch64.
import mowio

assert 1.5 + 2.25 == 3.75
assert 7.5 % 2.0 == 1.5
assert -7.5 % 2.0 == 0.5

assert round(2.5) == 2
assert round(3.5) == 4
assert round(-2.5) == -2
assert round(1.25, 1) == 1.2

assert 2.0 ** 10.0 == 1024.0
assert (-2.0) ** 3.0 == -8.0
assert (-2.0) ** 4.0 == 16.0
root = 9.0 ** 0.5
assert 2.99999999999999 < root < 3.00000000000001
assert mowio.sqrt(9.0) == 3.0

nan_value = float("nan")
inf_value = float("inf")
assert nan_value != nan_value
assert inf_value > 1.0e300
assert str(-0.0).startswith("-")

try:
    1.0 / 0.0
    raise AssertionError("float division by zero did not raise")
except ZeroDivisionError:
    pass

print("float smoke ok")
