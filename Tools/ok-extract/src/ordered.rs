//! Deterministic serialization for hash containers. Content-addressed
//! resource IDs and byte-exact checkpoints must not depend on per-instance
//! hash seeds (a material rebuilt on another thread or from a cold cache).
//! The wire format is unchanged: a map/sequence with the same entries, so
//! existing data still deserializes through the ordinary impls.
use serde::{Serialize,Serializer};
use std::collections::{BTreeMap,HashMap,HashSet};

pub(crate) fn map<K:Ord+Serialize,V:Serialize,H,S:Serializer>(value:&HashMap<K,V,H>,serializer:S)->Result<S::Ok,S::Error>{
    serializer.collect_map(value.iter().collect::<BTreeMap<_,_>>())
}
pub(crate) fn set<T:Ord+Serialize,H,S:Serializer>(value:&HashSet<T,H>,serializer:S)->Result<S::Ok,S::Error>{
    let mut sorted=value.iter().collect::<Vec<_>>();sorted.sort();
    serializer.collect_seq(sorted)
}

#[cfg(test)]mod tests{
    use super::*;
    #[derive(Serialize,serde::Deserialize,PartialEq,Debug)]
    struct Sample{#[serde(serialize_with="map")]m:HashMap<String,i32>,#[serde(serialize_with="set")]s:HashSet<i64>}
    #[derive(Serialize)]
    struct Plain{m:HashMap<String,i32>,s:HashSet<i64>}
    #[test]fn bytes_are_independent_of_hash_seed_and_compatible(){
        let build=||Sample{m:(0..64).map(|i|(format!("_Prop{i}"),i)).collect(),s:(0..64).collect()};
        let first=bincode::serialize(&build()).unwrap();
        for _ in 0..16{assert_eq!(bincode::serialize(&build()).unwrap(),first);}
        let value=build();
        let legacy=bincode::serialize(&Plain{m:value.m.clone(),s:value.s.clone()}).unwrap();
        assert_eq!(legacy.len(),first.len());
        assert_eq!(bincode::deserialize::<Sample>(&legacy).unwrap(),value);
        assert_eq!(bincode::deserialize::<Sample>(&first).unwrap(),value);
    }
}
