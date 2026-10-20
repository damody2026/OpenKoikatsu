import unittest
from street_city_correspondence import surface_displacement_bound
class CorrespondenceTests(unittest.TestCase):
 def test_large_steep_triangle_small_move(self):
  a=[(0,0,140),(275,2.5,12),(275,0,12)];b=[(0,0,140.001),a[1],a[2]];self.assertAlmostEqual(surface_displacement_bound(a,[(0,1,2)],b,[(0,1,2)]),.001)
 def test_changed_connectivity_rejected(self):
  a=[(0,0,0),(1,0,0),(0,1,0)];self.assertIsNone(surface_displacement_bound(a,[(0,1,2)],a,[(0,2,1)]))
 def test_far_geometry_not_certified_small(self):
  a=[(0,0,0),(1,0,0),(0,1,0)];b=[(0,0,1),a[1],a[2]];self.assertEqual(surface_displacement_bound(a,[(0,1,2)],b,[(0,1,2)]),1)
 def test_different_vertex_count_rejected(self):self.assertIsNone(surface_displacement_bound([(0,0,0)],[],[],[]))
if __name__=='__main__':unittest.main()
