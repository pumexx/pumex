# pumex::Asset class and its components

In short a pumex::Asset instance represents single 3D model, that we are able to read from file, or create ourselves procedurally.

Each asset contains :

* geometries ( meshes ) - **pumex::Geometry** class
* materials - **pumex::Material** class
* skeleton - **pumex::Skeleton** class
* animations - **pumex::Animation** class

Geometry is a collection of vertices and indices. Primitive topology ( defined by VkPrimitiveTopology enumeration ) describes how these vertices form primitives - triangles, triangle strips, lines, etc.

Every vertex in a geometry has the same vertex semantic, that describes how a collection of float values form a single vertex. Example vertex semantic, that defines a vertex, looks like this :

```
std::vector<pumex::VertexSemantic> requiredSemantic = 
{ 
  { pumex::VertexSemantic::Position, 3 },
  { pumex::VertexSemantic::Normal, 3 },
  { pumex::VertexSemantic::TexCoord, 2 } 
};
```

In this example our vertex consist of 8 float values: 3 for position, 3 for normal vector and 2 for texture coordinates.



Geometry has a material index which is index to materials vector in **pumex::Asset** instance.

Material consists of named properties ( in form of **glm::vec4** values ) and indexed textures. At the moment property names come from Assimp loader, for example:

- "$clr.ambient" property defines ambient color
- "$clr.diffuse"  property defines diffuse color
- "$clr.specular"  property defines specular color

Materials are not used directly during rendering. Instead of this they are transformed into user defined structures through MaterialSet class.



Skeleton is a tree of bones. Each bone has name and transformation in a form of 4x4 matrix. Skeleton defines "resting pose" of an Asset ( a pose in which vertices are defined ).

Each vertex must have at most 4 bones to which it is associated using special elements from vertex semantic : BoneWeight and BoneIndex.

BoneIndex determines a bone in a skeleton to which the vertex is associated. BoneWeight defines how strong is that association. Sum of BoneWeight coefficients must be equal to one. 

Asset's minimal skeleton consists of one bone with identity matrix. If there are no bone weights and bone indices defined in a vertex - it is assumed that each vertex points to a bone index 0, with weight = 1. 



Animation is a collection of animation channels. Each animation channel corresponds to a single bone in a skeleton and determines position and rotation of that bone in time. A set of channels determines animation of the whole asset. 



## Loading assets from file

Assets may be loaded from file using **pumex::AssetLoader** descendant. Currently only one such class is defined : **pumex::AssetLoaderAssimp** .

To load an asset use following **pumex::AssetLoaderAssimp** method: 

```
std::shared_ptr<Asset> load(std::shared_ptr<Viewer> viewer, const std::string& fileName, bool animationOnly = false, const std::vector<VertexSemantic>& requiredSemantic = std::vector<VertexSemantic>())
```

You may also change import flags defined for the loader using **pumex::AssetLoaderAssimp** method: 

```
  inline void setImportFlags(unsigned int flags);
```

Default flags defined for that importer are :

```
aiProcess_Triangulate | aiProcess_SortByPType | aiProcess_JoinIdenticalVertices
```



## Creating assets by hand

Assets may be created in code. There exists **pumex/utils/Shapes.h** header file that declares a set of handy functions for that specific purpose. The idea is that user creates a **pumex::Geometry** instance, then uses one of the functions defined in that header file to fill that geometry with specific shape. User may add this geometry to an pumex::Asset instance ( or use createSimpleAsset() function, that does it for him ) :

For example :

```
pumex::Geometry cylinder;
  cylinder.name          = "cylinder";
  cylinder.semantic      = { { pumex::VertexSemantic::Position, 3 }, { pumex::VertexSemantic::Normal, 3 }, { pumex::VertexSemantic::TexCoord, 2 } };
  
pumex::addCylinder(cylinder, glm::vec3(0.0, 0.0, 0.0), 1.0, 2.0, 20, true, true, true);

auto asset = createSimpleAsset(cylinder, "cylinderAsset")
```

More examples can be found in pumexgpucull example, where these functions are used to create trees, houses, cars, airplanes, etc.

