/*
 * localmotion2transform.c
 *
 *  Copyright (C) Georg Martius - January 2013
 *   georg dot martius at web dot de
 *
 *  This file is part of vid.stab video stabilization library
 *
 *  vid.stab is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License,
 *  as published by the Free Software Foundation; either version 2, or
 *  (at your option) any later version.
 *
 *  vid.stab is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "localmotion2transform.h"
#include "transformtype_operations.h"
#include <assert.h>
#include <string.h>

int vsLocalmotions2TransformsSimple(VSTransformData* td,
                                  const VSManyLocalMotions* motions,
                                  VSTransformations* trans ){
  int i;
  int len = vs_vector_size(motions);
  assert(trans->len==0 && trans->ts == 0);
  trans->ts = vs_malloc(sizeof(VSTransform)*len );
  for(i=0; i< vs_vector_size(motions); i++) {
    //   trans->ts[i]=vsSimpleMotionsToTransform(td,VSMLMGet(motions,i));
    // TODO: use a flag in td
    trans->ts[i]=vsMotionsToTransform(td,VSMLMGet(motions,i));
    //    vsStoreLocalmotions(stderr,VSMLMGet(motions,i));
    //    storeTransform(stderr,&trans->ts[i]);
  }
  trans->len=len;
  return VS_OK;
}

/* calculates rotation angle for the given transform and
 * field with respect to the given center-point
 */
double vsCalcAngle(const LocalMotion* lm, int center_x, int center_y){
  // we better ignore fields that are to close to the rotation center
  if (abs(lm->f.x - center_x) + abs(lm->f.y - center_y) < lm->f.size*2) {
    return 0;
  } else {
    // double r = sqrt(lm->f.x*lm->f.x + lm->f.y*lm->f.y);
    double a1 = atan2(lm->f.y - center_y, lm->f.x - center_x);
    double a2 = atan2(lm->f.y - center_y + lm->v.y,
                      lm->f.x - center_x + lm->v.x);
    double diff = a2 - a1;
    return (diff > M_PI) ? diff - 2 * M_PI : ((diff < -M_PI) ? diff + 2
                * M_PI : diff);
  }
}


VSTransform vsSimpleMotionsToTransform(VSTransformData* td,
                                   const LocalMotions* motions){
  int center_x = 0;
  int center_y = 0;
  VSTransform t = null_transform();
  if(motions==0) return t;
  int num_motions=vs_vector_size(motions);
  double *angles = (double*) vs_malloc(sizeof(double) * num_motions);
  LocalMotion meanmotion;
  int i;
  if(num_motions < 1)
    return t;

  // calc center point of all remaining fields
  for (i = 0; i < num_motions; i++) {
    center_x += LMGet(motions,i)->f.x;
    center_y += LMGet(motions,i)->f.y;
  }
  center_x /= num_motions;
  center_y /= num_motions;

  // cleaned mean
  meanmotion = cleanmean_localmotions(motions);

  // figure out angle
  if (num_motions < 6) {
    // the angle calculation is inaccurate for 5 and less fields
    t.alpha = 0;
  } else {
    for (i = 0; i < num_motions; i++) {
      // substract avg and calc angle
      LocalMotion m = sub_localmotion(LMGet(motions,i),&meanmotion);
      angles[i] = vsCalcAngle(&m, center_x, center_y);
    }
    double min, max;
    t.alpha = -cleanmean(angles, num_motions, &min, &max);
    if (max - min > td->maxAngleVariation) {
      t.alpha = 0;
      vs_log_info(td->conf.modName, "too large variation in angle(%f)\n",
      max-min);
    }
  }
  vs_free(angles);
  // compensate for off-center rotation
  double p_x = (center_x - td->fiSrc.width / 2);
  double p_y = (center_y - td->fiSrc.height / 2);
  t.x = meanmotion.v.x + (cos(t.alpha) - 1) * p_x - sin(t.alpha) * p_y;
  t.y = meanmotion.v.y + sin(t.alpha) * p_x + (cos(t.alpha) - 1) * p_y;

  return t;
}


// TODO; optimize
VSArray vsTransformToArray(const VSTransform* t){
  VSArray a = vs_array_new(4);
  a.dat[0] = t->x;
  a.dat[1] = t->y;
  a.dat[2] = t->alpha;
  a.dat[3] = t->zoom;
  return a;
}
// TODO; optimize
VSTransform vsArrayToTransform(VSArray a){
  return new_transform(a.dat[0],a.dat[1],a.dat[2],a.dat[3],0,0,0);
}

double sqr(double x){ return x*x; }

struct VSGradientDat {
  VSTransformData* td;
  const LocalMotions* motions;
};

double calcTransformQuality(VSArray params, void* dat){
  struct VSGradientDat* gd= (struct VSGradientDat*) dat;
  const LocalMotions* motions = gd->motions;
  int num_motions=vs_vector_size(motions);
  VSTransform t = vsArrayToTransform(params);
  double error=0;

  double z = 1.0+t.zoom/100.0;
  double zcos_a = z*cos(t.alpha); // scaled cos
  double zsin_a = z*sin(t.alpha); // scaled sin
  double c_x = gd->td->fiSrc.width / 2;
  double c_y = gd->td->fiSrc.height / 2;

  for (int i = 0; i < num_motions; i++) {
    LocalMotion* m = LMGet(motions,i);
    double x = m->f.x-c_x;
    double y = m->f.y-c_y;
    double vx =  zcos_a * x + zsin_a * y + t.x  - x;
    double vy  = -zsin_a * x + zcos_a * y + t.y - y;
    error += sqr(vx - m->v.x) +  sqr(vy - m->v.y);
  }
  return error/(num_motions+1) + t.alpha*t.alpha + t.zoom*t.zoom/10000.0;
}

VSTransform vsMotionsToTransform(VSTransformData* td,
                                 const LocalMotions* motions){
  VSTransform t = vsSimpleMotionsToTransform(td, motions);
  //VSTransform t = null_transform();
  if(motions==0) return t;


  VSArray params = vsTransformToArray(&t);
  double residual;
  struct VSGradientDat dat;
  dat.motions = motions;
  dat.td      = td;

  double ss[] = {0.2, 0.2, 0.0001, 0.1};
  VSArray result = vsGradientDescent(calcTransformQuality, params, &dat,
                                     20, vs_array(ss,4), 1e-15, &residual);
  vs_array_free(params);

  // now we need to ignore the fields that don't fit well (moving objects)
  // TODO
  vs_log_info(td->conf.modName, "residual(%f)\n",residual);

  t = vsArrayToTransform(result);
//  storeVSTransform(stderr, &t);
  vs_array_free(result);
  return t;
}



/* n-dimensional general purpose gradient descent algorithm */
VSArray vsGradientDescent(double (*eval)(VSArray, void*),
                         VSArray params, void* dat,
                         int N, VSArray stepsizes, double threshold, double* residual){
  int dim=params.len;
  double v = eval(params, dat);
  VSArray x = vs_array_copy(params);
  VSArray grad = vs_array_new(dim);
  assert(stepsizes.len == params.len);
  for(int i=0; i< N*dim && v > threshold; i++){
    int k=i%dim;
    VSArray x2 = vs_array_copy(x);
    double h = rand()%2 ? 1e-6 : -1e-6;
    x2.dat[k]+=h;
    double v2 = eval(x2, dat);
    vs_array_zero(&grad);
    grad.dat[k] = (v - v2)/h;
    vs_array_plus(&x2, x, *vs_array_scale(&x2, grad, stepsizes.dat[k]));
    v2 = eval(x2, dat);
//    printf("step: (%lf,%lf) :%g  \t(%g)\t [%i:%g]\n",x.dat[0],x.dat[1], v, v-v2,k,grad.dat[k]);
    if(v2 < v){
      fprintf(stderr,"+");
      vs_array_free(x);
      x = x2;
      v = v2;
      stepsizes.dat[k]*=1.2; // increase stepsize (4 successful steps will double it)
    }else{ // overshoot: reduce stepsize and don't do the step
      stepsizes.dat[k]/=2.0;
      vs_array_free(x2);
      fprintf(stderr,".");
    }
  }
  vs_array_free(grad);
  vs_array_free(stepsizes);
  if(residual != NULL) *residual=v;
  return x;
}



/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 *   c-basic-offset: 2 t
 * End:
 *
 * vim: expandtab shiftwidth=2:
 */
