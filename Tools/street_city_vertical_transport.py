"""Metric circulation contracts shared by large-building generators.

Banks are spatially distributed. Paired escalators keep opposite physical
slopes; their travel assignments alternate between storeys so an arriving
passenger can change lanes on the same landing to continue upwards.
"""
import math


def distributed_lift_banks(x=70, y=23, cars_per_bank=3, pitch=3.6):
    lifts=[]
    for side in [-1,1]:
        for end in [-1,1]:
            bank=('W' if side<0 else 'E')+('S' if end<0 else 'N')
            for i in range(cars_per_bank):
                lifts.append(dict(id=f'{bank}-{i+1}',bank=bank,x=side*x,
                    y=end*y+(i-(cars_per_bank-1)/2)*pitch,yaw=90*side*-1,
                    size=[3.2,5],car=[2.4,3.4],service=False))
    return lifts


def crisscross_escalators(floor_count=10, rise=4.2, bank_centres=(-8.1,-2.7,2.7,8.1)):
    run=rise/math.tan(math.radians(30));flights=[]
    for lower in range(1,floor_count):
        z=(lower-1)*rise
        for bank,cy in enumerate(bank_centres,1):
            for lane in [-1,1]:
                # A fixed physical slope per lane prevents intersecting
                # successive storeys. Travel direction changes by storey.
                slope=lane
                direction='up' if lane==(-1 if lower%2 else 1) else 'down'
                yy=cy+lane*.9
                flights.append(dict(id=f'ES{bank}-F{lower:02d}-{direction}',bank=f'ES{bank}',
                    lower_floor=lower,upper_floor=lower+1,lane=lane,direction=direction,
                    y=yy,slope_sign=slope,rise_m=rise,run_m=run,inclination_deg=30,
                    clear_width_m=1.2,riser_count=24,riser_m=rise/24,
                    lower_point=[-slope*run/2,yy,z],upper_point=[slope*run/2,yy,z+rise],
                    landing_edge_x=12,transfer_clear_width_m=2.4))
    return flights
