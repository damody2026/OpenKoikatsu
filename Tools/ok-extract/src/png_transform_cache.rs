//! Bounded cache for pure image operations. Keys include source bytes and
//! every operation parameter, so reuse never depends on a card or outfit ID.
use std::{collections::VecDeque,sync::{Mutex,OnceLock}};
use sha2::{Digest,Sha256};
// Outfit switches and cards sharing base textures reuse the converted maps; 1 GiB is small next to a card session.
const MAX_BYTES:usize=1024*1024*1024;
const MAX_ENTRIES:usize=2048;
#[derive(Default)]
struct Cache{entries:VecDeque<([u8;32],Vec<u8>)>,bytes:usize}
impl Cache {
 fn get(&mut self,key:&[u8;32])->Option<Vec<u8>>{
  let index=self.entries.iter().position(|(k,_)|k==key)?;
  let entry=self.entries.remove(index)?;let output=entry.1.clone();self.entries.push_back(entry);Some(output)
 }
 fn insert(&mut self,key:[u8;32],output:Vec<u8>){
  if output.len()>MAX_BYTES||self.entries.iter().any(|(k,_)|*k==key){return;}
  while self.entries.len()>=MAX_ENTRIES||self.bytes+output.len()>MAX_BYTES{
   let Some((_,old))=self.entries.pop_front()else{break};self.bytes-=old.len();
  }
  self.bytes+=output.len();self.entries.push_back((key,output));
 }
}
pub(crate) fn transform(operation:&str,source:&[u8],parameters:&[u8],compute:impl FnOnce()->Option<Vec<u8>>)->Option<Vec<u8>>{
 static CACHE:OnceLock<Mutex<Cache>>=OnceLock::new();
 let cache=CACHE.get_or_init(||Mutex::new(Cache::default()));
 let mut hash=Sha256::new();
 for field in [operation.as_bytes(),source,parameters]{hash.update((field.len() as u64).to_le_bytes());hash.update(field);}
 let key:[u8;32]=hash.finalize().into();
 use crate::memosnap::{TEX,get,put};
 if let Some(output)=cache.lock().unwrap_or_else(|e|e.into_inner()).get(&key){put(TEX,&key,||Some(output.clone()));return Some(output);}
 // Replayed from the card (OK_CARD_MEMO=tex).
 if let Some(output)=get(TEX,&key){let output=(*output).clone();cache.lock().unwrap_or_else(|e|e.into_inner()).insert(key,output.clone());return Some(output);}
 // Decode and encode outside the mutex; independent worker preparations do
 // not serialize behind an expensive cold transform.
 let output=compute()?;
 put(TEX,&key,||Some(output.clone()));
 cache.lock().unwrap_or_else(|e|e.into_inner()).insert(key,output.clone());Some(output)
}
#[cfg(test)]mod tests{
 use super::*;
 #[test]fn pure_transform_keys_include_operation_source_and_parameters(){
  let tag="cache-test-key-coverage";
  assert_eq!(transform(tag,b"a",b"1",||Some(vec![10])),Some(vec![10]));
  assert_eq!(transform(tag,b"a",b"1",||panic!("identical operation must reuse result")),Some(vec![10]));
  assert_eq!(transform(tag,b"b",b"1",||Some(vec![20])),Some(vec![20]));
  assert_eq!(transform(tag,b"a",b"2",||Some(vec![30])),Some(vec![30]));
  assert_eq!(transform("other-operation",b"a",b"1",||Some(vec![40])),Some(vec![40]));
 }
 #[test]fn cache_evicts_least_recently_used_and_accounts_bytes(){
  let mut c=Cache::default();
  for i in 0..MAX_ENTRIES{let mut k=[0;32];k[..8].copy_from_slice(&(i as u64).to_le_bytes());c.insert(k,vec![1]);}
  assert_eq!(c.bytes,MAX_ENTRIES);c.get(&[0;32]).unwrap();
  c.insert([255;32],vec![2;3]);let mut second=[0;32];second[0]=1;
  assert!(c.get(&second).is_none());assert!(c.get(&[0;32]).is_some());
  assert_eq!(c.entries.len(),MAX_ENTRIES);assert_eq!(c.bytes,MAX_ENTRIES+2);
  let mut c=Cache::default();c.insert([0;32],vec![1;MAX_BYTES/2+1]);c.insert([1;32],vec![2;MAX_BYTES/2+1]);
  assert_eq!(c.entries.len(),1);assert_eq!(c.bytes,MAX_BYTES/2+1);
 }
}
