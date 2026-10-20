//! Unreal's FRandomStream, bit for bit, so rules that were seeded in C++ (daily plans, timetables, applicants, weather)
//! give the same results after the move. Unseeded rolls (FMath::FRand in the old code) become a `seed` Unreal passes
//! with the request, so every answer is reproducible from its request.
#[derive(Debug, Clone)]
pub struct RandomStream {
    seed: u32,
}

impl RandomStream {
    pub fn new(seed: i32) -> Self {
        RandomStream { seed: seed as u32 }
    }

    fn mutate(&mut self) {
        self.seed = self.seed.wrapping_mul(196314165).wrapping_add(907633515);
    }

    /// 0 <= x < 1 (GetFraction / FRand).
    pub fn frand(&mut self) -> f32 {
        self.mutate();
        f32::from_bits(0x3F80_0000 | (self.seed >> 9)) - 1.0
    }

    /// GetUnsignedInt.
    pub fn unsigned(&mut self) -> u32 {
        self.mutate();
        self.seed
    }

    /// 0 .. a-1 (RandHelper).
    pub fn rand_helper(&mut self, a: i32) -> i32 {
        if a > 0 {
            (self.frand() * a as f32).trunc() as i32
        } else {
            0
        }
    }

    /// min ..= max (RandRange).
    pub fn rand_range(&mut self, min: i32, max: i32) -> i32 {
        min + self.rand_helper(max - min + 1)
    }

    /// FRandRange (Unreal computes it in double).
    pub fn frand_range(&mut self, min: f64, max: f64) -> f64 {
        min + (max - min) * self.frand() as f64
    }

    /// GetBool.
    pub fn bool(&mut self) -> bool {
        self.rand_helper(2) == 1
    }

    /// FRand() < p, how the old code rolls a chance.
    pub fn chance(&mut self, p: f32) -> bool {
        self.frand() < p
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn matches_unreal_lcg() {
        // Seed 1: first mutation 196314165 + 907633515 = 1103947680.
        let mut r = RandomStream::new(1);
        assert_eq!(r.unsigned(), 1_103_947_680);
        let mut r = RandomStream::new(1);
        let f = r.frand();
        assert_eq!(f, f32::from_bits(0x3F80_0000 | (1_103_947_680u32 >> 9)) - 1.0);
        assert!((0.0..1.0).contains(&f));
    }

    #[test]
    fn ranges_stay_inside() {
        let mut r = RandomStream::new(-12345);
        for _ in 0..10_000 {
            let v = r.rand_range(3, 7);
            assert!((3..=7).contains(&v));
            assert!(r.rand_helper(0) == 0);
        }
    }
}
