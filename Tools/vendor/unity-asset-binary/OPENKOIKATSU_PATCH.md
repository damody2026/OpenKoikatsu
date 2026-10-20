# Local unity-asset-binary 0.2.0 patch

Source: crates.io unity-asset-binary 0.2.0, upstream
https://github.com/Latias94/unity-asset , commit
f4a6bd6bbc3228f1b34a9c4b534e23e06836f286 (packaged .cargo_vcs_info.json).
License: MIT; upstream LICENSE included.

The upstream TypeTreeSerializer incorrectly applies its 1,000,000-element
typed-array limit to TypelessData byte blobs. Valid BP Profundis body vertex
data contains 1,218,064 bytes. Lenient parsing then loses m_VertexData and
misreads subsequent fields, causing the mesh to disappear.

Both parsing and PPtr scanning now check nonnegative blob length against the
remaining bytes of the containing object before reading/skipping. The separate
typed-array element limit remains unchanged. No fixed mesh name, vertex count,
or enlarged arbitrary threshold is used. Tests cover a large blob, the next
field, scanner alignment, negative length, oversized length, and truncation.

Only typetree/serializer.rs is changed from the packaged source. Cargo's local
patch applies consistently to binary parsing used by unity-asset-decode too;
the global Cargo registry is untouched.
