import numpy as np
import tensorflow as tf
from tensorflow.python.platform import test

from tf_big.python.ops.big_ops import big_import
from tf_big.python.ops.big_ops import big_export
from tf_big.python.ops.big_ops import big_add

class BigTest(test.TestCase):
  """BigTest test"""

  def test_import_export(self):
    with tf.Session() as sess:
      inp = [[b"43424"]]
      variant = big_import(inp)
      output = big_export(variant, tf.string)

      assert sess.run(output) == inp

  def test_import_export_int32(self):
    with tf.Session() as sess:
      inp = [[43424]]
      variant = big_import(inp)
      output = big_export(variant, tf.int32)

      expected = [[43424]]
      assert sess.run(output) == expected

  def test_add(self):
    with tf.Session() as sess:
      a = "5453452435245245245242534"
      b = "1424132412341234123412341234134"

      expected = int(a) + int(b)

      a_var = big_import([[a]])
      b_var = big_import([[b]])

      c_var = big_add(a_var, b_var)

      c_str = big_export(c_var, tf.string)

      output = sess.run(c_str)

      assert int(output) == expected

  def test_2d_matrix_add(self):
    with tf.Session() as sess:
      a = np.array([[b"5", b"5"], [b"5", b"5"]])
      b = np.array([[b"6", b"6"], [b"6", b"6"]])

      expected = a.astype(np.int32) + b.astype(np.int32)

      a_var = big_import(a)
      b_var = big_import(b)

      c_var = big_add(a_var, b_var)

      c_str = big_export(c_var, tf.string)

      output = sess.run(c_str)

      np.testing.assert_equal(output.astype(np.int32), expected)


if __name__ == '__main__':
  test.main()