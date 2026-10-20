//! Lock-free union-find with optimistic per-set locks.
//! Port of Wenzel Jakob's `dset.h` (Anderson & Woll, with a lock bit).

use std::sync::atomic::{AtomicU64, Ordering};

const LOCK: u64 = 1 << 63;

pub struct DisjointSets {
    data: Vec<AtomicU64>,
}

impl DisjointSets {
    pub fn new(size: usize) -> Self {
        let data = (0..size as u64).map(AtomicU64::new).collect();
        Self { data }
    }

    fn load(&self, id: u32) -> u64 {
        self.data[id as usize].load(Ordering::Acquire)
    }

    pub fn parent(&self, id: u32) -> u32 {
        self.load(id) as u32
    }

    pub fn rank(&self, id: u32) -> u32 {
        ((self.load(id) >> 32) as u32) & 0x7FFF_FFFF
    }

    pub fn find(&self, mut id: u32) -> u32 {
        while id != self.parent(id) {
            let value = self.load(id);
            let new_parent = self.parent(value as u32);
            let new_value = (value & 0xFFFF_FFFF_0000_0000) | u64::from(new_parent);
            if value != new_value {
                let _ = self.data[id as usize].compare_exchange_weak(
                    value,
                    new_value,
                    Ordering::AcqRel,
                    Ordering::Acquire,
                );
            }
            id = new_parent;
        }
        id
    }

    pub fn unite(&self, mut id1: u32, mut id2: u32) -> u32 {
        loop {
            id1 = self.find(id1);
            id2 = self.find(id2);
            if id1 == id2 {
                return id1;
            }
            let r1 = self.rank(id1);
            let r2 = self.rank(id2);
            if r1 > r2 || (r1 == r2 && id1 < id2) {
                std::mem::swap(&mut id1, &mut id2);
            }
            let r1 = self.rank(id1);
            let r2 = self.rank(id2);
            let old = (u64::from(r1) << 32) | u64::from(id1);
            let new = (u64::from(r1) << 32) | u64::from(id2);
            if self.data[id1 as usize]
                .compare_exchange(old, new, Ordering::AcqRel, Ordering::Acquire)
                .is_err()
            {
                continue;
            }
            if r1 == r2 {
                let old = (u64::from(r2) << 32) | u64::from(id2);
                let new = (u64::from(r2 + 1) << 32) | u64::from(id2);
                let _ = self.data[id2 as usize].compare_exchange_weak(
                    old,
                    new,
                    Ordering::AcqRel,
                    Ordering::Acquire,
                );
            }
            return id2;
        }
    }

    /// On success, `id` is replaced by the representative of its set.
    pub fn try_lock(&self, id: &mut u32) -> bool {
        *id = self.find(*id);
        let value = self.load(*id);
        if (value & LOCK) != 0 || (value as u32) != *id {
            return false;
        }
        self.data[*id as usize]
            .compare_exchange(value, value | LOCK, Ordering::AcqRel, Ordering::Acquire)
            .is_ok()
    }

    pub fn unlock(&self, id: u32) {
        self.data[id as usize].fetch_and(!LOCK, Ordering::AcqRel);
    }

    pub fn unite_index_locked(&self, id1: u32, id2: u32) -> u32 {
        let r1 = self.rank(id1);
        let r2 = self.rank(id2);
        if r1 > r2 || (r1 == r2 && id1 < id2) {
            id1
        } else {
            id2
        }
    }

    pub fn unite_unlock(&self, mut id1: u32, mut id2: u32) -> u32 {
        let mut r1 = self.rank(id1);
        let mut r2 = self.rank(id2);
        if r1 > r2 || (r1 == r2 && id1 < id2) {
            std::mem::swap(&mut r1, &mut r2);
            std::mem::swap(&mut id1, &mut id2);
        }
        self.data[id1 as usize].store((u64::from(r1) << 32) | u64::from(id2), Ordering::Release);
        let rank2 = r2 + if r1 == r2 { 1 } else { 0 };
        self.data[id2 as usize].store((u64::from(rank2) << 32) | u64::from(id2), Ordering::Release);
        id2
    }
}
