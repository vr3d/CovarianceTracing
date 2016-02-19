// STL includes
#include <cstdlib>
#include <cstdio>
#include <random>
#include <utility>

std::default_random_engine gen;
std::uniform_real_distribution<double> dist(0,1);

// Local includes
#include "common.hpp"

Material phong(Vector(), Vector(), Vector(1,1,1)*.999, 1000.);

Sphere spheres[] = {
   Sphere(Vector( 1e5+1,40.8,81.6),  1e5,  Vector(),Vector(.75,.25,.25)),//Left
   Sphere(Vector(-1e5+99,40.8,81.6), 1e5,  Vector(),Vector(.25,.25,.75)),//Rght
   Sphere(Vector(50,40.8, 1e5),      1e5,  Vector(),Vector(.75,.75,.75)),//Back
   Sphere(Vector(50,40.8,-1e5+170),  1e5,  Vector(),Vector()           ),//Frnt
   Sphere(Vector(50, 1e5, 81.6),     1e5,  Vector(),Vector(.75,.75,.75)),//Botm
   Sphere(Vector(50,-1e5+81.6,81.6), 1e5,  Vector(),Vector(.75,.75,.75)),//Top
   Sphere(Vector(27,16.5,47),        16.5, phong),//Mirr
   Sphere(Vector(73,16.5,78),        16.5, Vector(),Vector(1,1,1)*.999),//Glas
   Sphere(Vector(50,681.6-.27,81.6), 600,  Vector(12,12,12),  Vector()) //Lite
};


inline bool intersect(const Ray &r, double &t, int &id){
   double n=sizeof(spheres)/sizeof(Sphere), d, inf=t=1e20;
   for(int i=int(n);i--;) if((d=spheres[i].Intersect(r))&&d<t){t=d;id=i;}
   return t<inf;
}

using PosFilter = std::pair<Vector, Vector>;

PosFilter indirect_filter(const Ray &r, int depth, int maxdepth=2){
   double t;                               // distance to intersection
   int id=0;                               // id of intersected object
   if (!intersect(r, t, id)) return PosFilter(Vector(), Vector()); // if miss, return black
   const Sphere&   obj = spheres[id];      // the hit object
   const Material& mat = obj.mat;          // Its material
   Vector x  = r.o+r.d*t,
          n  = (x-obj.c).Normalize(),
          nl = Vector::Dot(n,r.d) < 0 ? n:n*-1;

   /* Local Frame at the surface of the object */
   Vector w = nl;

   // Once you reach the max depth, return the hit position and the filter's
   // value using the recursive form.
   if(depth >= maxdepth) {
      return PosFilter(x, Vector(1.0f, 1.0f, 1.0f));

   // Main covariance computation. First this code generate a new direction
   // and query the covariance+radiance in that direction. Then, it computes
   // the covariance after the reflection/refraction.
   } else {
      /* Sampling a new direction + recursive call */
      double pdf;
      const auto e  = Vector(dist(gen), dist(gen), dist(gen));
      const auto wo = -r.d;
      const auto wi = mat.Sample(wo, nl, e, pdf);
      auto f  = Vector::Dot(wi, nl)*mat.Reflectance(wi, wo, nl);
      /*
      double r1=2*M_PI*dist(gen), r2=dist(gen), r2s=sqrt(r2);
      Vector w = nl;
      Vector u = Vector::Cross((fabs(w.x) > .1 ? Vector(0,1,0) : Vector(1,0,0)), w).Normalize();
      Vector v = Vector::Cross(w, u);
      Vector d = (u*cos(r1)*r2s + v*sin(r1)*r2s + w*sqrt(1-r2)).Normalize();
      */
      const auto res = indirect_filter(Ray(x, wi), depth+1, maxdepth);
      return PosFilter(res.first, (1.f/pdf) * f.Multiply(res.second));
   }
}

// Covariance Tracing includes
#include <Covariance/Covariance4D.hpp>
using namespace Covariance;
using Cov    = Covariance4D<Vector>;
using PosCov = std::pair<Vector, Cov>;

PosCov CovarianceFilter(const Ray &r, const Cov& cov, int depth, int maxdepth=2) {
   double t;                               // distance to intersection
   int id=0;                               // id of intersected object
   if (!intersect(r, t, id)) return PosCov(Vector(), Cov()); // if miss, return black
   const Sphere&   obj = spheres[id];      // the hit object
   const Material& mat = obj.mat;          // Its material
   Vector x  = r.o+r.d*t,
          n  = (x-obj.c).Normalize(),
          nl = Vector::Dot(n,r.d) < 0 ? n:n*-1;
   const double k = 1.f/spheres[id].r;

   // Update the covariance with travel and project it onto the tangent plane
   // of the hit object.
   Cov cov2 = cov;
   cov2.Travel(t);
   cov2.Projection(n);

   // if the max depth is reached
   if(depth >= maxdepth) {
      cov2.matrix[1] = - cov2.matrix[1];
      return PosCov(x, cov2);
   } else {

      // Sample a new direction
      auto wi = -r.d;
      auto wr = 2*Vector::Dot(wi, nl)*nl - wi;
      auto r2 = Ray(x, wr);

      cov2.Curvature(k, k);
      cov2.Cosine(1.0f);
      cov2.Symmetry();
      const double rho = mat.exponent / (4*M_PI*M_PI);
      cov2.Reflection(rho, rho);
      cov2.Curvature(-k, -k);
      cov2.InverseProjection(wr);
      return CovarianceFilter(r2, cov2, depth+1, maxdepth);
   }
}


#include <xmmintrin.h>

int main(int argc, char** argv){
   int w=512, h=512, samps = argc==2 ? atoi(argv[1])/4 : 1; // # samples
   Ray cam(Vector(50,52,295.6), Vector(0,-0.042612,-1).Normalize()); // cam pos, dir
   cam.o = cam.o + 140.0*cam.d;
   std::cout << "Camera o=" << cam.o << " and d=" << cam.d << std::endl;

   double fov   = 1.2; // 0.5135;
   Vector  cx  = Vector(w*fov/h);
   Vector  cy  = Vector::Cross(cx, cam.d).Normalize()*fov;
   Vector ncx  = cx; ncx.Normalize();
   Vector ncy  = cy; ncy.Normalize();
   Vector* img = new Vector[w*h];

   const double sigma = .5f;
   const double fact  = 1.f / (sqrt(2.*M_PI)*sigma);

   _MM_SET_EXCEPTION_MASK(_MM_GET_EXCEPTION_MASK() & ~_MM_MASK_INVALID);

   // Loop over the rows and columns of the image and evaluate radiance and
   // covariance per pixel using Monte-Carlo.
   //*
   int x=200;
   int y=210;
   /*/
   int x=164;
   int y=218;
   //*/
   std::vector<PosFilter> _filter_elems;

   // Sub pixel sampling
   for (int sy=0, i=(h-y-1)*w+x; sy<2; sy++) {
      for (int sx=0; sx<2; sx++){

         for (int s=0; s<samps; s++){

            // Generate a sub-pixel random position to perform super
            // sampling.
            double r1=2*dist(gen), dx=r1<1 ? sqrt(r1)-1: 1-sqrt(2-r1);
            double r2=2*dist(gen), dy=r2<1 ? sqrt(r2)-1: 1-sqrt(2-r2);

            // Generate the pixel direction
            Vector d = cx*( ( (sx+.5 + dx)/2 + x)/w - .5) +
                       cy*( ( (sy+.5 + dy)/2 + y)/h - .5) + cam.d;
            d.Normalize();

            // Covariance tracing requires to know the pixel frame in order to
            // align the orientation of the covariance matrix with respect to
            // the image plane. (cx, cy, d) is not a proper frame and we need
            // to correct it.
            const Vector px = (cx - Vector::Dot(d, cx)*d).Normalize(),
                         py = (cy - Vector::Dot(d, cy)*d).Normalize();
            const double scaleX = Vector::Norm(cx) / double(w),
                        scaleY = Vector::Norm(cy) / double(h);

            // Evaluate the Covariance and Radiance at the pixel location
            _filter_elems.push_back(indirect_filter(Ray(cam.o, d), 0, 1));
         }
      }
   }

   // Generate a covariance matrix at the sampling position
   const auto t = (cx*((x+0.5)/double(w) - .5) + cy*((y+0.5)/double(h) - .5) + cam.d).Normalize();
   const auto u = (ncx - Vector::Dot(t, ncx)*t).Normalize();
   const auto v = (ncy - Vector::Dot(t, ncy)*t).Normalize();
   const auto pixelCov = Cov({ 1.0E5, 0.0, 1.0E5, 0.0, 0.0, 1.0E5, 0.0, 0.0, 0.0, 1.0E5 }, t);
   auto surfCov  = CovarianceFilter(Ray(cam.o, t), pixelCov, 0, 1);
   std::cout << "Hit point for covariance: " << surfCov.second.matrix[0]
             << ", " << surfCov.second.matrix[1]
             << ", " << surfCov.second.matrix[2] << std::endl;

   // Loop over the rows and columns of the image and evaluate radiance and
   // covariance per pixel using Monte-Carlo.
   #pragma omp parallel for schedule(dynamic, 1) private(gen)
   for (int y=0; y<h; y++){
      fprintf(stderr,"\rRendering (%d spp) %5.2f%%",samps*4,100.*y/(h-1));
      for (int x=0; x<w; x++) {
         int i=(h-y-1)*w+x;
         Vector _r;

         // Sub pixel sampling
         for (int sy=0; sy<2; sy++) {
            for (int sx=0; sx<2; sx++){
               // Generate the pixel direction
               Vector d = cx*( ( (sx+.5)/2. + x)/w - .5) +
                          cy*( ( (sy+.5)/2. + y)/h - .5) + cam.d;
               d.Normalize();

               Ray ray(cam.o, d);
               double t; int id;
               if(!intersect(ray, t, id)){ continue; }
               Vector hitp = ray.o + t*ray.d;

               for(auto& elem : _filter_elems) {
                  const auto& p = elem.first;
                  _r = _r + (0.25 / sigma) /*fact*/ *exp(-(.5f/(sigma*sigma))*pow(Vector::Norm(hitp-p), 2))*Vector(1,0,0);//elem.second;
               }
               _r = (1.f/samps)*_r;

               // Evaluate the covariance
               const Vector dx  = surfCov.first - hitp;
               const double du  = Vector::Dot(dx, surfCov.second.x);
               const double dv  = Vector::Dot(dx, surfCov.second.y);
               const double dt  = Vector::Dot(dx, surfCov.second.z);
               double det = surfCov.second.matrix[0]*surfCov.second.matrix[2]
                          - pow(surfCov.second.matrix[1], 2);
               double bf  = du*du*surfCov.second.matrix[2]
                          + dv*dv*surfCov.second.matrix[0]
                          - 2*du*dv*surfCov.second.matrix[1];

               det = 1.0;
               bf  = du*du+dv*dv;
               _r = _r + 0.25 * exp(-10.0*dt*dt) * exp(- 0.5* bf/det) * Vector(0,0,1);
            }
         }

         img[i] = _r;
      }
   }
   int index = (h-y-1)*w+x;
   img[index] = Vector(0,1,0);

   // Output image
   const auto ret = SaveEXR(img, w, h, "image.exr");

   delete[] img;
   return ret;
}