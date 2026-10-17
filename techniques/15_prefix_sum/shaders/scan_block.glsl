// Shared by the two block-scan kernels: what a 512-element block reads and writes. Elements past the level's
// end read as 0 (the last block is partial), and are not written back.
layout(local_size_x = 256) in;

uint load(uint i) { return i < pc.counts.x ? data[pc.counts.y + i] : 0u; }
void store(uint i, uint v) {
  if (i < pc.counts.x) data[pc.counts.y + i] = v;
}
// The block's total feeds the next level up.
void storeBlockSum(uint total) { data[pc.counts.z + gl_WorkGroupID.x] = total; }
