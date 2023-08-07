unit_member: u32;

add_to_member :: (this_unit: &$U, n: u32) {
    this_unit.unit_member = this_unit.unit_member + n;
}
