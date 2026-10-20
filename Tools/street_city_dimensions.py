"""Current user-authored metre policy; distinct from historical references."""
WALL_M=.2
CORRIDOR_M=2.0
SIDE_AISLE_GROSS_M=CORRIDOR_M+2*WALL_M
CORE_BAND_M=6+SIDE_AISLE_GROSS_M
SIDE_AISLE_OFFSET_M=6+SIDE_AISLE_GROSS_M/2
DOOR_M=1.2
MAIN_DOOR_M=2.4
PUBLIC_CONCOURSE_BUILDINGS={'M1','H1','CV','TR','AD','SC','LI','IB'}

def hall_depth(bid):
    # These are occupied open public foyers, with a separate2m passage.
    return 8.0 if bid in PUBLIC_CONCOURSE_BUILDINGS else CORRIDOR_M
