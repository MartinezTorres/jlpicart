//https://solder-stencil.me/

$fn=72;

module Tower() {
    
    difference() { union() {
        translate([-10,0,-20]) cube([20,70,40]);
        for ( i = [0:5:26] ) {
            translate([9.9,i*2.5,-.25-5]) cube([2,0.5,.5]);    
            translate([9.9,i*2.5,-.25+5]) cube([2,0.5,.5]);    
        }
    } union() {

        translate([-9.5,-1,-19.5]) cube([19,70,19.125]);
        translate([-9.5,-1,-19.5+19.625]) cube([19,70,19.125]);
        
    } }
    
}


module M3_12() {
    
    cylinder(0.25,5.7/2,5.7/2,$fn=24);
    translate([0,0,.245]) cylinder(1.7,5.7/2,3/2,$fn=24);
    cylinder(9,3.4/2,3.4/2,$fn=24);
    cylinder(15.6,3/2,3/2,$fn=24);
    
}

module M3_12_S() {
    
    cylinder(0.25,5.7/2,5.7/2,$fn=24);
    translate([0,0,.245]) cylinder(1.7,5.7/2,3/2,$fn=24);
    cylinder(9,3.4/2,3.4/2,$fn=24);
    cylinder(14.5,3/2,3/2,$fn=24);
    
}

module support() {
    intersection() {
        translate([-1,0,0]) cube([3,20,10]);
        translate([0,6,0]) rotate([60,0,0]) translate([0,-15,-10]) cube([1,18.0,20]);
    }
}

module bottom_cart() {
    
    difference() {
    union() {

        translate ([-1.4,-1.35,7.5])  {
            color([1,0,0]) 
            rotate([90,0,0]) 
            rotate([0,270,0]) 
            translate ([-12,25,-25]) 
            import("files/CartBottom.stl", convexity=3);
        }
        
        translate([4.5,20.6,-5.8]) support();
        translate([27.125, 24.3,-6.9]) support();
        translate([83.375, 19.1,-6.9]) support();

        for ( i = [1:1:9] ) {
            translate([15.375 + i*8,9.9,-6.9]) support();
        }

        translate([21.55,9.9,-5.8]) support();
        translate([89.075,9.9,-5.8]) support();
        translate([101,9.9,-5.8]) support();

        translate([42.15,65.7,4.15-3.01-8.2]) cube([50.5,2.51,8.2]);

        translate([0,0,-8.2]) {
            translate([0,65.7,4.15-3.01]) cube([100,1.0,8.2]);

            translate([-2.025,9.5+10.5,4.15-3.01]) cube([4.9,57.2-10.5,8.2]);
            translate([-2.025+5.2,9.5+10.5,4.15-3.01]) cube([4.9,57.2-10.5,8.2]);


            translate([-2.025,9.5-10.78,4.15-3.01]) cube([2.5,57.3-10.5,8.2]);
                
            translate([-2.55+96.5,9.5,4.15-3.01]) cube([4.9,57.2,8.2]);
            translate([-2.55+96.5+5.2,9.5,4.15-3.01]) cube([4.9,57.2,8.2]);
            translate([-2.55+94+10,9.5-10.78,4.15-3.01]) cube([2.5,57.3,8.2]);
        }

        for ( i = [0:1:22] ) {
            hull() {
                translate([i*2.2,67.2107-1.5,4.15-3.01-8]) cube([1,1.0,8]);
                translate([i*2.2,67.2107,4.15-3.01-6.5]) cube([1,1.0,6.5]);
            }
        }
        
        if (0) {
            translate([-2,66.211,4.15-0.6]) cube([4,2,0.6]);

            translate([19,66.211,4.15-0.6]) cube([4,2,0.6]);
            translate([30,66.211,4.15-0.6]) cube([4,2,0.6]);

            translate([19+49.5,66.211,4.15-0.6]) cube([4,2,0.6]);
            translate([30+49.5,66.211,4.15-0.6]) cube([4,2,0.6]);

            translate([100,66.211,4.15-0.6]) cube([4,2,0.6]);
        }
        
    }
    union() {

        translate([-5,-5,4.15-2.99]) cube([110,80,10]);
    }
    }
}

module top_cart() {

    difference() {
        union() {
            translate ([-1.4,-1.35,7.5])  {

                color([1,0,0]) 
                rotate([90,0,0]) 
                rotate([0,270,0]) 
                translate ([-12,25,-25]) 
                import("files/CartTop.stl", convexity=3);
            }


            translate([4.5,20.6,-5.8+14.]) rotate([0,180,0]) translate([-1,0,0]) support();
            translate([27.125, 23.75,-6.9+15.7]) rotate([0,180,0]) translate([-1,0,0]) support();
            translate([83.375, 18.6,-6.9+15.7]) rotate([0,180,0]) translate([-1,0,0]) support();

            for ( i = [1:1:9] ) {
                translate([15.375 + i*8,9.9,-5.8+14.575]) rotate([0,180,0]) translate([-1,0,0]) support();
            }
            
            translate([21.55,9.9,-6.5+14.575]) rotate([0,180,0]) translate([-1,0,0]) support();
            translate([89.075,9.9,-6.5+14.575]) rotate([0,180,0]) translate([-1,0,0]) support();
            translate([101,9.9,-6.5+14.575]) rotate([0,180,0]) translate([-1,0,0]) support();


        
            
        translate([0,0,0]) {
            translate([0,65.7,4.15-3.01]) cube([100,1.0,8.2]);

            translate([-2.025,9.5+10.5,4.15-3.01]) cube([4.9,57.2-10.5,8.2]);
            translate([-2.025+5.2,9.5+10.5,4.15-3.01]) cube([4.9,57.2-10.5,8.2]);


            translate([-2.025,9.5-10.78,4.15-3.01]) cube([2.5,57.3-10.5,8.2]);            
            translate([-2.025-1.49,9.5-10.78,4.15-3.01+1]) cube([2.5,57.3+9,6.2]);
                
            translate([-2.55+96.5,9.5,4.15-3.01]) cube([4.9,57.2,8.2]);
            translate([-2.55+96.5+5.2,9.5,4.15-3.01]) cube([4.9,57.2,8.2]);
            translate([-2.55+94+10,9.5-10.78,4.15-3.01]) cube([2.5,57.3,8.2]);

            translate([-2.55+94+10+1.52,9.5-10.78,4.15-3.01+1]) cube([2.5,57.3+9,6.2]);
        }
        for ( i = [0:1:45] ) {
            hull() {
                translate([i*2.2,67.2107-1.5,4.15-3.01]) cube([1,1.0,8]);
                translate([i*2.2,67.2107,4.15-3.01]) cube([1,1.0,6.5]);
            }
        }


        }
        //translate([20,10.211,-1.3]) cube([69,15,3]);
        translate([-5,-5,4.15-3.01-10]) cube([112,80,10]);
    }
    
}

module pcb() {

    if (0) translate ([13.4,34,0])  {

        color([0,0.5,0]) 
        rotate([0,0,0]) 
        rotate([0,180,0]) 
        translate ([-200,100,0]) 
        import("ImageToStl.com_3d_minipi_2025-02-13.stl", convexity=3);
    }
    
    translate ([13.4,34,0])  {
        
        translate([6.95,-24,0.050]) union() {
            cube([75,49.5,1.6]);
//            translate([0, 20,-0.6]) cube([75,29,2.8]);
            translate([2.5,-10,0]) cube([66,10,1.6]);
        }

        hull() {
            color([0,0.75,0]) 
            rotate([0,180,0]) 
            translate ([-61.5,22.5,-0.0]) 
            cube([31,8,13]);

            color([0,0.95,0]) 
            rotate([0,180,0]) 
            translate ([-61.5,15.5,-0.0]) 
            cube([31,8,10]);


            color([0,0.95,0]) 
            rotate([0,180,0]) 
            translate ([-61.5,2,+3.0]) 
            cube([31,8,1]);
        }

            color([0,0.25,0]) 
            rotate([0,180,0]) 
            translate ([-63,30.49,-2.0+0.8]) 
            cube([34,8,16-0.8]);

        hull() {
            color([0,0.95,0]) 
            rotate([0,180,0]) 
            translate ([-77.3,15.5,1]) 
            cube([14.35,20,7]);

            color([0,0.95,0]) 
            rotate([0,180,0]) 
            translate ([-77.3,2,+3.0]) 
            cube([14.35,15,1]);            
        }

        hull() {
            if(0) color([0.8,0.0,0]) 
            rotate([0,180,0]) 
            translate ([-25.5+1.5,13.65+1.5,-17.0]) 
            cube([25-3,11-3,10]);

            color([0.8,0.0,0]) 
            rotate([0,180,0]) 
            translate ([-25.5-11,13.65-11,-16.0]) 
            cube([25+22,11+22,1]);

            color([0.8,0.0,0]) 
            rotate([0,180,0]) 
            translate ([-23.5+1.5,13.65+2,-18.0]) 
            cube([23-3,11-4,10]);
        }

        color([0.8,0.0,0]) 
        rotate([0,180,0]) 
        translate ([-23.5+1.5,13.65+2,-17.0]) 
        cube([23-3,11-4,10]);
    }
}


module hull_vga() {
    difference() {
        union() {

            hull() {
                minkowski() {
                    
                    translate ([13.4,34,0])  {

                        color([0,0.75,0]) 
                        rotate([0,180,0]) 
                        translate ([-61.5,22.5,-0.0]) 
                        cube([31,10,13]);

                        color([0,0.95,0]) 
                        rotate([0,180,0]) 
                        translate ([-61.5,15.5,-0.0]) 
                        cube([31,8,10]);

                        color([0,0.95,0]) 
                        rotate([0,180,0]) 
                        translate ([-61.5,7,+3.0]) 
                        cube([31,8,1]);
                    }
                    sphere(2.75);
                }
            }

            hull() {
                minkowski() {
                    
                    translate ([13.4,34,0])  {

                        color([0,0.95,0]) 
                        rotate([0,180,0]) 
                        translate ([-77.6,15.5,-0.0]) 
                        cube([15,17,8]);
 
                        color([0,0.95,0]) 
                        rotate([0,180,0]) 
                        translate ([-77.6,7,+3.0]) 
                        cube([15,15,1]);
                    }
                    sphere(2.75);
                }
            }
        }
        
        translate ([13.4,34,0])  {

            color([0,0.75,0]) 
            rotate([0,180,0]) 
            translate ([-85.5,-10,-8.0]) 
            cube([67,60,13]);

            color([0,0.75,0]) 
            rotate([0,180,0]) 
            translate ([-85.5,34.211,-8.0]) 
            cube([67,60,25]);

        }
        
        
        
    }
    

    color([0,0.75,0]) 
    translate ([40.9,51.5,-7]) 
    cube([3,15,8.15]);

    color([0,0.75,0]) 
    translate ([40.9,51.5,-7+7.06]) 
    cube([54,15,8.15-7.06]);

    color([0,0.75,0]) 
    translate ([74.9,51.5,-7]) 
    cube([1.5,15,8.15]);


}





translate([-14.5,-1.29,0.25]) Tower();
translate([116.5,-1.29,0.25]) rotate([0,180,0]) Tower();

if(1) translate([0,0,-5]) difference() { union() {

    bottom_cart();
    hull_vga();

    translate([27.65,20.9,-7.5125+0.05]) translate([-3,-3,0]) cube([6,6,2.4]);
    translate([83.88,15.7,-7.5125+0.05]) translate([-3,-3,0]) cube([6,6,2.4]);

    translate([-0.1,0,0])hull() {
        translate([0,3,0.5]) rotate([-90,0,0]) cylinder(60,1.75,1.75,$fn=6);
        translate([0,1,0.5]) rotate([-90,0,0]) cylinder(64,0.25,0.25,$fn=6);
    }
    translate([102.1,0,0]) hull() {
        translate([0,3,0.5]) rotate([-90,0,0]) cylinder(60,1.75,1.75,$fn=6);
        translate([0,1,0.5]) rotate([-90,0,0]) cylinder(64,0.25,0.25,$fn=6);
    }
} union() {

    pcb();
    translate([-2.025+5.1,28,-7.5125]) M3_12();
    translate([-2.025+5.1,44,-7.5125]) M3_12();
    translate([-2.025+5.1,60,-7.5125]) M3_12();

    translate([-2.55+96.5+5.1,12,-7.5125]) M3_12();
    translate([-2.55+96.5+5.1,28,-7.5125]) M3_12();
    translate([-2.55+96.5+5.1,44,-7.5125]) M3_12();
    translate([-2.55+96.5+5.1,60,-7.5125]) M3_12();
    
    translate([27.65,20.9,-7.5125]) M3_12_S();
    translate([83.88,15.7,-7.5125]) M3_12_S();
    
    if(0) {
        translate([-0.1,0,0])hull() {
            translate([0,3,0.5]) rotate([-90,0,0]) cylinder(60,1.75,1.75,$fn=6);
            translate([0,1,0.5]) rotate([-90,0,0]) cylinder(64,0.25,0.25,$fn=6);
        }
        translate([102.1,0,0]) hull() {
            translate([0,3,0.5]) rotate([-90,0,0]) cylinder(60,1.75,1.75,$fn=6);
            translate([0,1,0.5]) rotate([-90,0,0]) cylinder(64,0.25,0.25,$fn=6);
        }
    }
} }


if(1) translate([0,0,3.25]) difference() { union() {

    top_cart();

} union() {
    
    pcb();
    translate([-2.025+5.1,28,-7.5125]) M3_12();
    translate([-2.025+5.1,44,-7.5125]) M3_12();
    translate([-2.025+5.1,60,-7.5125]) M3_12();

    translate([-2.55+96.5+5.1,12,-7.5125]) M3_12();
    translate([-2.55+96.5+5.1,28,-7.5125]) M3_12();
    translate([-2.55+96.5+5.1,44,-7.5125]) M3_12();
    translate([-2.55+96.5+5.1,60,-7.5125]) M3_12();
    
    translate([27.65,20.9,-7.5125]) M3_12_S();
    translate([83.88,15.7,-7.5125]) M3_12_S();
    
    if(1) {
        translate([-0.1,0,0])hull() {
            translate([0,3,0.5]) rotate([-90,0,0]) cylinder(60,1.75,1.75,$fn=6);
            translate([0,1,0.5]) rotate([-90,0,0]) cylinder(64,0.25,0.25,$fn=6);
        }
        translate([102.1,0,0]) hull() {
            translate([0,3,0.5]) rotate([-90,0,0]) cylinder(60,1.75,1.75,$fn=6);
            translate([0,1,0.5]) rotate([-90,0,0]) cylinder(64,0.25,0.25,$fn=6);
        }
    }
}
}


