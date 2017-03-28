#Set working directory
setwd("C:/Users/intern/Documents/GitHub/frc-vision-2017/calibrationLogs/")

#Install required libraries
#install.packages("rgl")
library(rgl)

#Read X, Y, Z data from an actual .csv file (Note: 1st row must include header names)
dat = read.csv("4ft_straight_real2.log.csv")
x = dat$x
y = dat$y
z = dat$width

#Center data around (0,0)
x = x-320
y = y-240 

#Remove datapoints too near to the edge that are likely skewed by only seeing a partial target
df = data.frame(x,y,z)
#df = df[(df$x>-270 & df$x<270 & df$y>-220 & df&y<220 & (abs(df$x)>100 | z<85)),] # 7ft cleanup
#df = df[(df$x>-245 & df$x<245 & df$y>-220 & df&y<100 & z>105),] # 4ft cleanup

#Remove "NA" values that have somehow appeared
df = df[complete.cases(df),]


#Start calculating stuff!

x2=abs(df$x)^2
y2=abs(df$y)^2
zLinFit = lm(df$z~x2 + y2)
summary(zLinFit)
#plot(zLinFit)

#m=nls(z~a*x^2 + c*y^2 + e, data=df, start=c(a=.0001,c=.0001,e=68))
#summary(m)



#a spinning 3D plot
plot3d(df, col="blue", size=4)


#7ft data summary
#Call:
#lm(formula = df$z ~ x2 + y2)

#Residuals:
#    Min      1Q  Median      3Q     Max 
#-6.8204 -1.2419 -0.6867  0.7503 19.8867 

#Coefficients:
#             Estimate Std. Error t value Pr(>|t|)    
#(Intercept) 7.150e+01  3.298e-02 2167.95   <2e-16 ***
#x2          3.391e-04  1.116e-06  303.84   <2e-16 ***
#y2          1.853e-04  3.663e-06   50.59   <2e-16 ***
#---
#Signif. codes:  0 ‘***’ 0.001 ‘**’ 0.01 ‘*’ 0.05 ‘.’ 0.1 ‘ ’ 1

#Residual standard error: 2.37 on 9248 degrees of freedom
#Multiple R-squared:  0.9209,    Adjusted R-squared:  0.9209 
#F-statistic: 5.385e+04 on 2 and 9248 DF,  p-value: < 2.2e-16