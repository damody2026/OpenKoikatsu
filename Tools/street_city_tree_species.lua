-- Visual tree families; bounded game art inspired by Taiwanese landscape trees.
local old={
 {'camphor-a',731,7,6.5,8,false}, {'camphor-b',1051,7,6.5,8,false},
 {'camphor-c',1429,7,6.5,8,false}, {'flower-a',937,5.5,5,6,true}
}
local extended={};for _,v in ipairs(old)do extended[#extended+1]=v end
for _,v in ipairs({
 {'banyan',1801,7,6.5,8,false,'broad'},
 {'terminalia',1823,6.6,6.2,8,false,'tiered'},
 {'columnar',1861,3.4,3.2,8,false,'columnar'},
 {'cypress',1877,4.8,4.5,8,true,'conifer'},
 {'willow',1901,6.8,6.4,8,false,'weeping'},
 {'palm',1931,6.6,6.2,8,true,'palm'},
 {'golden-flower',1951,5.5,5,6,true,'golden'},
 {'white-flower',1973,5.2,4.8,6,true,'white'}
})do extended[#extended+1]=v end
return {v1=old,v2=extended}
