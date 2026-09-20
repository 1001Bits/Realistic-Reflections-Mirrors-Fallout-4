#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <DirectXMath.h>

struct MirrorShadowCasterVolume
{
    using Point = DirectX::XMFLOAT3;
    struct Plane { double x{},y{},z{},w{}; };

    std::array<Plane,30> planes{};
    unsigned planeCount{6};
    std::array<Point,16> vertices{}; 
    Point origin{};
    double filterGuard{};

    static constexpr double ReuseGuard=128;

    static constexpr double PruneMargin=512;
    bool valid{};

    static bool Finite(const Point& p) noexcept
    { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
    double Distance(const Plane& p,const Point& v) const noexcept
    { return p.x*(double(v.x)-origin.x)+p.y*(double(v.y)-origin.y)+p.z*(double(v.z)-origin.z)+p.w; }

    static bool Inverse(const DirectX::XMFLOAT4X4& matrix,double (&inverse)[4][4]) noexcept
    {

        double rows[4][8]{};
        for(unsigned i=0;i<4;++i) for(unsigned j=0;j<4;++j) {
            rows[i][j]=matrix.m[i][j];rows[i][j+4]=i==j?1.:0.;
        }
        for(unsigned column=0;column<4;++column) {
            unsigned pivot=column;
            for(unsigned row=column+1;row<4;++row)
                if(std::fabs(rows[row][column])>std::fabs(rows[pivot][column])) pivot=row;
            if(!std::isfinite(rows[pivot][column]) || std::fabs(rows[pivot][column])<1.e-15) return false;
            if(pivot!=column) for(unsigned j=0;j<8;++j) std::swap(rows[column][j],rows[pivot][j]);
            const double divisor=rows[column][column];
            for(double& value:rows[column]) value/=divisor;
            for(unsigned row=0;row<4;++row) if(row!=column) {
                const double scale=rows[row][column];
                for(unsigned j=0;j<8;++j) rows[row][j]-=scale*rows[column][j];
            }
        }
        for(unsigned i=0;i<4;++i) for(unsigned j=0;j<4;++j) {
            inverse[i][j]=rows[i][j+4];
            if(!std::isfinite(inverse[i][j])) return false;
        }
        return true;
    }

    static MirrorShadowCasterVolume Build(const DirectX::XMFLOAT4X4& viewProjection,
        const Point& eye,const Point& lightRays,float travel,float guard) noexcept
    {
        using namespace DirectX;
        MirrorShadowCasterVolume out;
        if(!Finite(eye) || !Finite(lightRays) || !std::isfinite(travel) || travel<=0 ||
            !std::isfinite(guard) || guard<0) return out;
        for(const auto& row:viewProjection.m) for(float n:row) if(!std::isfinite(n)) return out;
        const double rayLength=std::sqrt(double(lightRays.x)*lightRays.x+
            double(lightRays.y)*lightRays.y+double(lightRays.z)*lightRays.z);
        if(rayLength<1.e-8 || !std::isfinite(rayLength)) return out;
        const double dx=lightRays.x/rayLength,dy=lightRays.y/rayLength,dz=lightRays.z/rayLength;
        out.origin=eye;out.filterGuard=guard;
        for(unsigned i=0;i<6;++i) {
            double p[4]{};
            for(unsigned row=0;row<4;++row)
                p[row]=i==4?viewProjection.m[row][2]:
                    double(viewProjection.m[row][3])+(i%2?-1:1)*double(viewProjection.m[row][i/2]);
            const double length=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
            if(length<1.e-10 || !std::isfinite(length)) return {};
            auto& plane=out.planes[i];
            plane={p[0]/length,p[1]/length,p[2]/length,p[3]/length};

            plane.w+=std::max(0.,plane.x*dx+plane.y*dy+plane.z*dz)*travel;
        }
        double inverse[4][4]{};
        if(!Inverse(viewProjection,inverse)) return {};
        for(unsigned i=0;i<8;++i) {
            const double clip[4]{i&1?1.:-1.,i&2?1.:-1.,i&4?1.:0.,1.};
            double corner[4]{};
            for(unsigned column=0;column<4;++column) for(unsigned row=0;row<4;++row)
                corner[column]+=clip[row]*inverse[row][column];
            if(!std::isfinite(corner[3]) || corner[3]<=1.e-12) return {};
            auto& point=out.vertices[i];
            point={float(corner[0]/corner[3]+eye.x),float(corner[1]/corner[3]+eye.y),float(corner[2]/corner[3]+eye.z)};
            out.vertices[i+8]={float(point.x-dx*travel),float(point.y-dy*travel),float(point.z-dz*travel)};
            if(!Finite(point) || !Finite(out.vertices[i+8])) return {};
        }

        for(unsigned bit=1;bit<=4;bit<<=1) for(unsigned i=0;i<8;++i) {
            if(i&bit) continue;
            const auto& a=out.vertices[i];const auto& b=out.vertices[i|bit];
            const double ex=double(b.x)-a.x,ey=double(b.y)-a.y,ez=double(b.z)-a.z;
            double nx=ey*dz-ez*dy,ny=ez*dx-ex*dz,nz=ex*dy-ey*dx;
            const double length=std::sqrt(nx*nx+ny*ny+nz*nz);
            if(!std::isfinite(length) || length<1.e-8) continue;
            nx/=length;ny/=length;nz/=length;
            for(double sign:{-1.,1.}) {
                const Plane normal{nx*sign,ny*sign,nz*sign,0};
                bool duplicate=false;
                for(unsigned j=6;j<out.planeCount;++j) {
                    const auto& prior=out.planes[j];
                    if(prior.x*normal.x+prior.y*normal.y+prior.z*normal.z>1.-1.e-10) {
                        duplicate=true;break;
                    }
                }
                if(duplicate) continue; 
                double minimum=out.Distance(normal,out.vertices[0]);
                for(const auto& vertex:out.vertices)
                    minimum=std::min(minimum,out.Distance(normal,vertex));
                if(!std::isfinite(minimum)) continue;
                out.planes[out.planeCount++]={normal.x,normal.y,normal.z,-minimum};
            }
        }
        out.valid=true;

        if(!out.Covers(out)) return {};
        return out;
    }

    bool Intersects(const Point& center,float radius) const noexcept
    {
        
        if(!valid || !Finite(center) || !std::isfinite(radius) || radius<=1) return true;
        for(unsigned i=0;i<planeCount;++i)
            if(Distance(planes[i],center)<-(double(radius)+filterGuard+ReuseGuard+PruneMargin)) return false;
        return true;
    }
    bool Covers(const MirrorShadowCasterVolume& requested) const noexcept
    {
        if(!valid) return true; 
        if(!requested.valid) return false;

        for(const auto& vertex:requested.vertices) for(unsigned i=0;i<planeCount;++i)
            if(Distance(planes[i],vertex)<requested.filterGuard-filterGuard-ReuseGuard) return false;
        return true;
    }
};
