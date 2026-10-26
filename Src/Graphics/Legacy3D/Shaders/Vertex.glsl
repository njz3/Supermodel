/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011-2012 Bart Trzynadlowski, Nik Henson 
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free 
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/
  
/*
 * Vertex.glsl
 *
 * Vertex shader for 3D rendering.
 */
 
#version 120

// Global uniforms
uniform mat4	modelViewMatrix;	// model -> view space matrix
uniform mat4	projectionMatrix;	// view space -> screen space matrix
uniform vec3	lighting[2];		// lighting state (lighting[0] = sun direction, lighting[1].x,y = diffuse, ambient intensities from 0-1.0)
uniform vec4	spotEllipse;		// spotlight ellipse position: .x=X position (normalized device coordinates), .y=Y position, .z=half-width, .w=half-height)
uniform vec2	spotRange;			// spotlight Z range: .x=start (viewspace coordinates), .y=limit
uniform vec3	spotColor;			// spotlight RGB color
uniform bool	intensityClamp;		// light intensity at most 1 (Step 1.0; Step 1.5 on drives it beyond, as New3D)
uniform bool	step15Luminous;		// Step 1.5: luminous polygons brighter, as New3D
uniform float	layerDepthBias;		// layered polygons: depth as if 1+layerDepthBias times closer

// Custom vertex attributes
attribute vec4	subTexture;		// .x=texture X, .y=texture Y, .z=texture width, .w=texture height (all in texels)
attribute vec4	texParams;		// .x=texture enable (if 1, else 0), .y=use transparency (if >=0), .z=U wrap mode (1=mirror, 0=repeat), .w=V wrap mode
attribute float	texFormat;		// T1RGB5 contour texture (if > 0)
attribute float texMap;         // texture map number
attribute float	transLevel;		// translucence level, 0.0 (transparent) to 1.0 (opaque). if less than 1.0, replace alpha value
attribute float	lightEnable;	// lit (1.0; 2.0 fixed shaded: the shade in the normal's X), luminous (0.0; -1.0 fixed shaded)
attribute float	specular;		  // specular coefficient (0.0 if disabled)
attribute float	shininess;		// specular shininess
attribute float	fogIntensity;	// fog intensity (1.0, full fog effect, 0.0, no fog) 

// Custom outputs to fragment shader
varying vec4	fsSubTexture;
varying vec4	fsTexParams;
varying float	fsTexFormat;
varying float	fsTexMap;
varying float	fsTransLevel;
varying vec3	fsLightIntensity;	// total light intensity for this vertex
varying float	fsSpecularTerm;		// specular light term (additive)
varying float	fsFogFactor;		// fog factor
varying float	fsViewZ;

// Gets the 3x3 matrix out of a 4x4 (because mat3(mat4matrix) does not work on ATI!)
mat3 GetLinearPart( mat4 m )
{
	mat3 result;
	
	result[0][0] = m[0][0]; 
	result[0][1] = m[0][1]; 
	result[0][2] = m[0][2]; 

	result[1][0] = m[1][0]; 
	result[1][1] = m[1][1]; 
	result[1][2] = m[1][2]; 
	
	result[2][0] = m[2][0]; 
	result[2][1] = m[2][1]; 
	result[2][2] = m[2][2]; 
	
	return result;
}

void main(void)
{
	vec3	viewVertex;		// vertex coordinates in view space
	vec3	viewNormal;		// vertex normal in view space
	vec3	sunVector;		// sun lighting vector (as reflecting away from vertex)
	float	sunFactor;		// sun light projection along vertex normal (0.0 to 1.0)
	vec3	halfway;
	float	specFactor;
	
	// Transform vertex
	gl_Position = projectionMatrix * modelViewMatrix * gl_Vertex;
	gl_Position.z += projectionMatrix[3][2] * layerDepthBias;	// the near plane term, 1+layerDepthBias times
	viewVertex = vec3(modelViewMatrix * gl_Vertex);	
	
	/*
	 * Modulation
	 *
 	 * Polygon color serves as material color (modulating the light intensity)
	 * for textured polygons. The fragment shader will ignore (overwrite) the
	 * the color passed to it if the fragment is textured. 
	 *
	 * Untextured fragments must be set to the polygon color and the light
	 * intensity is initialized to 1.0 here. Alpha must be set to 1.0 because
	 * the fragment shader multiplies it by the polygon translucency setting. 
	 *
	 * TO-DO: Does OpenGL set alpha to 1.0 by default if no alpha is specified
	 * for the vertex? If so, we can remove that line from here.
	 */

	gl_FrontColor = gl_Color;	// untextured polygons will use this
	gl_FrontColor.a = 1.0;	
	fsLightIntensity = vec3(1.0,1.0,1.0);
	if (texParams.x > 0.5)		// textured
		fsLightIntensity *= gl_Color.rgb;
		
	/*
 	 * Sun Light
	 *
	 * Parallel light source and ambient lighting are only applied for non-
	 * luminous polygons.
 	 */
	fsSpecularTerm = 0.0;
 	if (lightEnable > 0.5)	// not luminous
	{
		// Normal -> view space
		viewNormal = normalize(GetLinearPart(modelViewMatrix)*gl_Normal);

		// Real3D -> OpenGL view space convention (TO-DO: do this outside of shader)
		sunVector = lighting[0]*vec3(1.0,-1.0,-1.0);
		
		// Compute diffuse factor for sunlight (fixed shaded polygons: their shade, as New3D)
		sunFactor = (lightEnable > 1.5) ? max(gl_Normal.x,0.0) : max(dot(sunVector,viewNormal),0.0);
		
		// Total light intensity: sum of all components
		float intensity = sunFactor*lighting[1].x+lighting[1].y;
		fsLightIntensity *= intensityClamp ? min(1.0,intensity) : intensity;
		
		/*
		 * Specular Lighting
		 *
		 * The specular term is treated similarly to the "separate specular
		 * color" functionality of OpenGL: it is added as a highlight in the
		 * fragment shader. This allows even black textures to be lit.
		 *
		 * TO-DO: Ambient intensity viewport parameter is known but what about
		 * the intensity of the specular term? Always applied with full 
		 * intensity here but this is unlikely to be correct.
		 */
  		if (shininess >= 0.0 && lightEnable < 1.5)	// fixed shaded polygons have no normal
  		{
  			// As New3D (Model 3): smooth shaded polygons N.L to the power shininess (its multiplier is in specular),
  			// flat shaded ones the reflection's Z (the view vector taken as Z), both times the sun intensity
  			float s = (shininess > 0.0) ? pow(sunFactor,shininess) : max(reflect(-sunVector,viewNormal).z,0.0);
  			fsSpecularTerm = specular*lighting[1].x*s;

  			/*
  			vec3 V = normalize(-viewVertex);
  			vec3 H = normalize(sunVector+V);	// halfway vector
  			float s = max(10.0,64.0-shininess);	// seems to look nice, but probably not correct
  			fsSpecularTerm = pow(max(dot(viewNormal,H),0.0),s);
  			if (sunFactor <= 0.0) fsSpecularTerm = 0.0;
  			*/
  			
  			// Faster approximation  			
  			//float temp = max(dot(viewNormal,H),0.0);
  			//float s = 64.0-shininess;
  			//fsSpecularTerm = temp/(s-temp*s+temp);
  			
  			// Phong formula
  			//vec3 R = normalize(2.0*dot(sunVector,viewNormal)*viewNormal - sunVector);
  			//vec3 V = normalize(-viewVertex);
  			//float s = max(2.0,64.0-shininess);
  			//fsSpecularTerm = pow(max(dot(R,V),0.0),s);
  		}
	}
	else if (step15Luminous)
	{
		// Step 1.5 luminous polygons, as New3D: times 1 + fixed shade + ambient if fixed shaded, else 1.5
		fsLightIntensity *= (lightEnable < -0.5) ? 1.0+gl_Normal.x+lighting[1].y : 1.5;
	}
	
	// Fog
	float z = length(viewVertex);
	fsFogFactor = fogIntensity*clamp(gl_Fog.start+z*gl_Fog.density, 0.0, 1.0);

	// Pass viewspace Z coordinate (for spotlight)
	fsViewZ = -viewVertex.z;	// convert Z from GL->Real3D convention (want +Z to be further into screen)

	// Pass remaining parameters to fragment shader
	gl_TexCoord[0] = gl_MultiTexCoord0;
	fsSubTexture = subTexture;
	fsTexParams = texParams;
	fsTransLevel = transLevel;
	fsTexFormat = texFormat;
	fsTexMap = texMap;
}
