/*
===========================================================================
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

This file is part of Spearmint Source Code.

Spearmint Source Code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 3 of the License,
or (at your option) any later version.

Spearmint Source Code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Spearmint Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, Spearmint Source Code is also subject to certain additional terms.
You should have received a copy of these additional terms immediately following
the terms and conditions of the GNU General Public License.  If not, please
request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional
terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc.,
Suite 120, Rockville, Maryland 20850 USA.
===========================================================================
*/
// tr_mesh.c: triangle model functions

#include "tr_local.h"

static float ProjectRadius( float r, vec3_t location )
{
	float pr;
	float dist;
	float c;
	vec3_t	p;
	float	projected[4];

	c = DotProduct( tr.viewParms.or.axis[0], tr.viewParms.or.origin );
	dist = DotProduct( tr.viewParms.or.axis[0], location ) - c;

	if ( dist <= 0 )
		return 0;

	p[0] = 0;
	p[1] = fabs( r );
	p[2] = -dist;

	projected[0] = p[0] * tr.viewParms.projectionMatrix[0] + 
		           p[1] * tr.viewParms.projectionMatrix[4] +
				   p[2] * tr.viewParms.projectionMatrix[8] +
				   tr.viewParms.projectionMatrix[12];

	projected[1] = p[0] * tr.viewParms.projectionMatrix[1] + 
		           p[1] * tr.viewParms.projectionMatrix[5] +
				   p[2] * tr.viewParms.projectionMatrix[9] +
				   tr.viewParms.projectionMatrix[13];

	projected[2] = p[0] * tr.viewParms.projectionMatrix[2] + 
		           p[1] * tr.viewParms.projectionMatrix[6] +
				   p[2] * tr.viewParms.projectionMatrix[10] +
				   tr.viewParms.projectionMatrix[14];

	projected[3] = p[0] * tr.viewParms.projectionMatrix[3] + 
		           p[1] * tr.viewParms.projectionMatrix[7] +
				   p[2] * tr.viewParms.projectionMatrix[11] +
				   tr.viewParms.projectionMatrix[15];


	pr = projected[1] / projected[3];

	if ( pr > 1.0f )
		pr = 1.0f;

	return pr;
}

/*
=============
R_CullModel
=============
*/
static int R_CullModel( mdvModel_t *model, trRefEntity_t *ent ) {
	vec3_t		bounds[2];
	mdvFrame_t	*oldFrame, *newFrame;
	int			i;

	// compute frame pointers
	newFrame = model->frames + ent->e.frame;
	oldFrame = model->frames + ent->e.oldframe;

	// cull bounding sphere ONLY if this is not an upscaled entity
	if ( !ent->e.nonNormalizedAxes )
	{
		if ( ent->e.frame == ent->e.oldframe )
		{
			switch ( R_CullLocalPointAndRadius( newFrame->localOrigin, newFrame->radius ) )
			{
			case CULL_OUT:
				tr.pc.c_sphere_cull_md3_out++;
				return CULL_OUT;

			case CULL_IN:
				tr.pc.c_sphere_cull_md3_in++;
				return CULL_IN;

			case CULL_CLIP:
				tr.pc.c_sphere_cull_md3_clip++;
				break;
			}
		}
		else
		{
			int sphereCull, sphereCullB;

			sphereCull  = R_CullLocalPointAndRadius( newFrame->localOrigin, newFrame->radius );
			if ( newFrame == oldFrame ) {
				sphereCullB = sphereCull;
			} else {
				sphereCullB = R_CullLocalPointAndRadius( oldFrame->localOrigin, oldFrame->radius );
			}

			if ( sphereCull == sphereCullB )
			{
				if ( sphereCull == CULL_OUT )
				{
					tr.pc.c_sphere_cull_md3_out++;
					return CULL_OUT;
				}
				else if ( sphereCull == CULL_IN )
				{
					tr.pc.c_sphere_cull_md3_in++;
					return CULL_IN;
				}
				else
				{
					tr.pc.c_sphere_cull_md3_clip++;
				}
			}
		}
	}
	
	// calculate a bounding box in the current coordinate system
	for (i = 0 ; i < 3 ; i++) {
		bounds[0][i] = oldFrame->bounds[0][i] < newFrame->bounds[0][i] ? oldFrame->bounds[0][i] : newFrame->bounds[0][i];
		bounds[1][i] = oldFrame->bounds[1][i] > newFrame->bounds[1][i] ? oldFrame->bounds[1][i] : newFrame->bounds[1][i];
	}

	switch ( R_CullLocalBox( bounds ) )
	{
	case CULL_IN:
		tr.pc.c_box_cull_md3_in++;
		return CULL_IN;
	case CULL_CLIP:
		tr.pc.c_box_cull_md3_clip++;
		return CULL_CLIP;
	case CULL_OUT:
	default:
		tr.pc.c_box_cull_md3_out++;
		return CULL_OUT;
	}
}


/*
=================
R_ComputeLOD

=================
*/
int R_ComputeLOD( trRefEntity_t *ent ) {
	float radius;
	float flod, lodscale;
	float projectedRadius;
	mdvFrame_t *frame;
	mdrHeader_t *mdr;
	mdrFrame_t *mdrframe;
	int lod;

	if ( tr.currentModel->numLods < 2 )
	{
		// model has only 1 LOD level, skip computations and bias
		lod = 0;
	}
	else
	{
		// multiple LODs exist, so compute projected bounding sphere
		// and use that as a criteria for selecting LOD

		if(tr.currentModel->type == MOD_MDR)
		{
			int frameSize;
			mdr = (mdrHeader_t *) tr.currentModel->modelData;
			frameSize = (size_t) (&((mdrFrame_t *)0)->bones[mdr->numBones]);
			
			mdrframe = (mdrFrame_t *) ((byte *) mdr + mdr->ofsFrames + frameSize * ent->e.frame);
			
			radius = RadiusFromBounds(mdrframe->bounds[0], mdrframe->bounds[1]);
		}
		else
		{
			//frame = ( md3Frame_t * ) ( ( ( unsigned char * ) tr.currentModel->md3[0] ) + tr.currentModel->md3[0]->ofsFrames );
			frame = tr.currentModel->mdv[0]->frames;

			frame += ent->e.frame;

			radius = RadiusFromBounds( frame->bounds[0], frame->bounds[1] );
		}

		if ( ( projectedRadius = ProjectRadius( radius, ent->e.origin ) ) != 0 )
		{
			lodscale = r_lodscale->value;
			if (lodscale > 20) lodscale = 20;
			flod = 1.0f - projectedRadius * lodscale;
		}
		else
		{
			// object intersects near view plane, e.g. view weapon
			flod = 0;
		}

		flod *= tr.currentModel->numLods;
		lod = ri.ftol(flod);

		if ( lod < 0 )
		{
			lod = 0;
		}
		else if ( lod >= tr.currentModel->numLods )
		{
			lod = tr.currentModel->numLods - 1;
		}
	}

	lod += r_lodbias->integer;
	
	if ( lod >= tr.currentModel->numLods )
		lod = tr.currentModel->numLods - 1;
	if ( lod < 0 )
		lod = 0;

	return lod;
}

/*
=================
R_ComputeFogNum

=================
*/
int R_ComputeFogNum( mdvModel_t *model, trRefEntity_t *ent ) {
	mdvFrame_t		*mdvFrame;
	vec3_t			localOrigin;

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return 0;
	}

	// FIXME: non-normalized axis issues
	mdvFrame = model->frames + ent->e.frame;
	VectorAdd( ent->e.origin, mdvFrame->localOrigin, localOrigin );

	return R_PointFogNum( &tr.refdef, localOrigin, mdvFrame->radius );
}

/*
=================
R_AddMD3Surfaces

=================
*/
void R_AddMD3Surfaces( trRefEntity_t *ent ) {
	int				i;
	mdvModel_t		*model;
	mdvSurface_t	*surface;
	void			*drawSurf;
	shader_t		*shader;
	int				cull;
	int				lod;
	int				fogNum;
	int             cubemapIndex;
	qboolean		personalModel;

	// don't add mirror only objects if not in a mirror/portal
	personalModel = (ent->e.renderfx & RF_ONLY_MIRROR) && !(tr.viewParms.isPortal 
	                 || (tr.viewParms.flags & (VPF_SHADOWMAP | VPF_DEPTHSHADOW)));

	if ( ent->e.renderfx & RF_WRAP_FRAMES ) {
		ent->e.frame %= tr.currentModel->mdv[0]->numFrames;
		ent->e.oldframe %= tr.currentModel->mdv[0]->numFrames;
	}

	//
	// Validate the frames so there is no chance of a crash.
	// This will write directly into the entity structure, so
	// when the surfaces are rendered, they don't need to be
	// range checked again.
	//
	if ( (ent->e.frame >= tr.currentModel->mdv[0]->numFrames) 
		|| (ent->e.frame < 0)
		|| (ent->e.oldframe >= tr.currentModel->mdv[0]->numFrames)
		|| (ent->e.oldframe < 0) ) {
			ri.Printf( PRINT_DEVELOPER, "R_AddMD3Surfaces: no such frame %d to %d for '%s'\n",
				ent->e.oldframe, ent->e.frame,
				tr.currentModel->name );
			ent->e.frame = 0;
			ent->e.oldframe = 0;
	}

	//
	// compute LOD
	//
	lod = R_ComputeLOD( ent );

	model = tr.currentModel->mdv[lod];

	//
	// cull the entire model if merged bounding box of both frames
	// is outside the view frustum.
	//
	cull = R_CullModel ( model, ent );
	if ( cull == CULL_OUT ) {
		return;
	}

	//
	// set up lighting now that we know we aren't culled
	//
	if ( !personalModel || r_shadows->integer > 1 ) {
		R_SetupEntityLighting( &tr.refdef, ent );
	}

	//
	// see if we are in a fog volume
	//
	fogNum = R_ComputeFogNum( model, ent );

	cubemapIndex = R_CubemapForPoint(ent->e.origin);

	//
	// draw all surfaces
	//
	surface = model->surfaces;
	for ( i = 0 ; i < model->numSurfaces ; i++ ) {

		if ( ent->e.customShader || ent->e.customSkin ) {
			shader = R_CustomSurfaceShader( surface->name, ent->e.customShader, ent->e.customSkin );
			if ( shader == tr.nodrawShader ) {
				surface++;
				continue;
			}
		} else if ( surface->numShaderIndexes <= 0 ) {
			shader = tr.defaultShader;
		} else {
			shader = tr.shaders[ surface->shaderIndexes[ ent->e.skinNum % surface->numShaderIndexes ] ];
		}

		if ( model->numVaoSurfaces > 0 ) {
			drawSurf = &model->vaoSurfaces[i];
		} else {
			drawSurf = surface;
		}

		// we will add shadows even if the main object isn't visible in the view

		// stencil shadows can't do personal models unless I polyhedron clip
		if ( !personalModel
			&& r_shadows->integer == 2 
			&& fogNum == 0
			&& !(ent->e.renderfx & ( RF_NOSHADOW | RF_DEPTHHACK ) ) 
			&& shader->sort == SS_OPAQUE ) {
			R_AddDrawSurf( drawSurf, tr.shadowShader, 0, qfalse, qfalse, 0 );
		}

		// projection shadows work fine with personal models
		if ( r_shadows->integer == 3
			&& fogNum == 0
			&& (ent->e.renderfx & RF_SHADOW_PLANE )
			&& shader->sort == SS_OPAQUE ) {
			R_AddDrawSurf( drawSurf, tr.projectionShadowShader, 0, qfalse, qfalse, 0 );
		}

		// don't add third_person objects if not viewing through a portal
		if ( !personalModel ) {
			R_AddEntDrawSurf( ent, drawSurf, shader, fogNum, qfalse, 0, qfalse, cubemapIndex );
		}

		surface++;
	}

}





