//! A global allocator for the command-line tools: large blocks (textures, PNGs, decoded bundles) are kept when freed
//! and handed out again, instead of going back to the system and coming back as new zeroed pages. Windows serves
//! blocks of about a megabyte and more with VirtualAlloc, so every texture an open made cost page faults on first touch
//! and a VirtualFree after: a fifth of a card conversion's CPU was kernel time. Smaller blocks go to the system heap.
//! Not used in the game (the static library keeps the host's allocator).
use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::Mutex;

/// Blocks from this size are recycled.
const LARGE: usize = 1 << 20;
/// Freed blocks kept at most, in bytes (past it a freed block goes back to the system).
const KEEP: usize = 2 << 30;

pub struct RecyclingAlloc;

struct Cache { bytes: usize, classes: Vec<(usize, usize, Vec<usize>)> }

static CACHE: Mutex<Cache> = Mutex::new(Cache { bytes: 0, classes: Vec::new() });

/// The block size kept for a request: its power of two. Eighths of a power of two made so many sizes that a card
/// converted after another found few of its sizes kept, and a block of an unused size stayed kept (C080: 626K page
/// faults a card, 343K with powers of two and eviction; peak private memory 6.2 -> 5.9 GB). The rounding is
/// only committed, not touched.
fn class(size: usize) -> usize {
    size.next_power_of_two()
}

unsafe impl GlobalAlloc for RecyclingAlloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if layout.size() < LARGE { return System.alloc(layout); }
        let size = class(layout.size());
        if let Ok(mut cache) = CACHE.lock() {
            if let Some((_, _, free)) = cache.classes.iter_mut().find(|(s, a, _)| *s == size && *a == layout.align()) {
                if let Some(block) = free.pop() { cache.bytes -= size; return block as *mut u8; }
            }
        }
        System.alloc(Layout::from_size_align_unchecked(size, layout.align()))
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        if layout.size() < LARGE { return System.alloc_zeroed(layout); }
        let block = self.alloc(layout);
        if !block.is_null() { std::ptr::write_bytes(block, 0, layout.size()); }
        block
    }

    unsafe fn dealloc(&self, block: *mut u8, layout: Layout) {
        if layout.size() < LARGE { return System.dealloc(block, layout); }
        let size = class(layout.size());
        if let Ok(mut cache) = CACHE.lock() {
            // Full: the sizes kept most give way (the sizes an open asks for change from card to card; keeping the
            // first ones forever sent every other size back to the system).
            while cache.bytes + size > KEEP {
                let Some(i) = cache.classes.iter().enumerate()
                    .filter(|(_, (s, a, free))| !free.is_empty() && !(*s == size && *a == layout.align()))
                    .max_by_key(|(_, (s, _, free))| s * free.len()).map(|(i, _)| i) else { break };
                let (s, a, free) = &mut cache.classes[i];
                let (s, a) = (*s, *a);
                let Some(old) = free.pop() else { break };
                cache.bytes -= s;
                System.dealloc(old as *mut u8, Layout::from_size_align_unchecked(s, a));
            }
            if cache.bytes + size <= KEEP {
                cache.bytes += size;
                match cache.classes.iter_mut().find(|(s, a, _)| *s == size && *a == layout.align()) {
                    Some((_, _, free)) => free.push(block as usize),
                    None => cache.classes.push((size, layout.align(), vec![block as usize])),
                }
                return;
            }
        }
        System.dealloc(block, Layout::from_size_align_unchecked(size, layout.align()))
    }

    unsafe fn realloc(&self, block: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        // Within the same kept size: nothing to move.
        if layout.size() >= LARGE && new_size >= LARGE && class(layout.size()) == class(new_size) { return block; }
        if layout.size() < LARGE && new_size < LARGE { return System.realloc(block, layout, new_size); }
        let new = self.alloc(Layout::from_size_align_unchecked(new_size, layout.align()));
        if !new.is_null() {
            std::ptr::copy_nonoverlapping(block, new, layout.size().min(new_size));
            self.dealloc(block, layout);
        }
        new
    }
}
