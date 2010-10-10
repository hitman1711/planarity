/*
Planarity-Related Graph Algorithms Project
Copyright (c) 1997-2010, John M. Boyer
All rights reserved. Includes a reference implementation of the following:

* John M. Boyer. "Simplified O(n) Algorithms for Planar Graph Embedding,
  Kuratowski Subgraph Isolation, and Related Problems". Ph.D. Dissertation,
  University of Victoria, 2001.

* John M. Boyer and Wendy J. Myrvold. "On the Cutting Edge: Simplified O(n)
  Planarity by Edge Addition". Journal of Graph Algorithms and Applications,
  Vol. 8, No. 3, pp. 241-273, 2004.

* John M. Boyer. "A New Method for Efficiently Generating Planar Graph
  Visibility Representations". In P. Eades and P. Healy, editors,
  Proceedings of the 13th International Conference on Graph Drawing 2005,
  Lecture Notes Comput. Sci., Volume 3843, pp. 508-511, Springer-Verlag, 2006.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

* Neither the name of the Planarity-Related Graph Algorithms Project nor the names
  of its contributors may be used to endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>

#include "graph.h"

/* Imported functions */

extern void _ClearVertexVisitedFlags(graphP theGraph, int);

extern int _IsolateKuratowskiSubgraph(graphP theGraph, int I, int R);
extern int _IsolateOuterplanarObstruction(graphP theGraph, int I, int R);

/* Private functions (some are exported to system only) */

void _CreateSortedSeparatedDFSChildLists(graphP theGraph);
int  _CreateFwdArcLists(graphP theGraph);
void _CreateDFSTreeEmbedding(graphP theGraph);

void _EmbedBackEdgeToDescendant(graphP theGraph, int RootSide, int RootVertex, int W, int WPrevLink);

int  _GetNextVertexOnExternalFace(graphP theGraph, int curVertex, int *pPrevLink);

void _InvertVertex(graphP theGraph, int V);
void _MergeVertex(graphP theGraph, int W, int WPrevLink, int R);
int  _MergeBicomps(graphP theGraph, int I, int RootVertex, int W, int WPrevLink);

void _WalkUp(graphP theGraph, int I, int J);
int  _WalkDown(graphP theGraph, int I, int RootVertex);

int  _HandleBlockedEmbedIteration(graphP theGraph, int I);
int  _EmbedPostprocess(graphP theGraph, int I, int edgeEmbeddingResult);

int  _OrientVerticesInEmbedding(graphP theGraph);
int  _OrientVerticesInBicomp(graphP theGraph, int BicompRoot, int PreserveSigns);
int  _JoinBicomps(graphP theGraph);

/********************************************************************
 _EmbeddingInitialize()

 This method performs the following tasks:
 (1) Assign depth first index (DFI) and DFS parentvalues to vertices
 (2) Assign DFS edge types
 (3) Create a sortedDFSChildList for each vertex, sorted by child DFI
 (4) Create a sortedFwdArcList for each vertex, sorted by descendant DFI
 (5) Assign leastAncestor values to vertices
 (6) Sort the vertices by their DFIs
 (7) Embed each tree edge as a singleton biconnected component

 The first five of these are performed in a single-pass DFS of theGraph.
 Afterward, the vertices are sorted by their DFIs, the lowpoint values
 are assigned and then the DFS tree edges stored in virtual vertices
 during the DFS are used to create the DFS tree embedding.
 ********************************************************************/
int  _EmbeddingInitialize(graphP theGraph)
{
	stackP theStack;
	int N, DFI, I, R, uparent, u, uneighbor, e, J, JTwin, JPrev, JNext;
	/**/
	int leastValue, child;
	/***/

#ifdef PROFILE
platform_time start, end;
platform_GetTime(start);
#endif

	gp_LogLine("graphEmbed.c/_EmbeddingInitialize() start\n");

	N = theGraph->N;
	theStack  = theGraph->theStack;

	// At most we push 2 integers per edge from a vertex to each *unvisited* neighbor
	// plus one additional integer to help detect post-processing.  This is less
	// than the 2 * arcCapacity integer stack that is already in theGraph structure,
	// so we make sure it's still there and cleared, then we clear all vertex
	// visited flags in prep for the Depth first search operation. */

	if (sp_GetCapacity(theStack) < 2*gp_GetArcCapacity(theGraph))
		return NOTOK;

	sp_ClearStack(theStack);

	_ClearVertexVisitedFlags(theGraph, FALSE);

	// This outer loop processes each connected component of a disconnected graph
	// No need to compare I < N since DFI will reach N when inner loop processes the
	// last connected component in the graph
	for (I=DFI=0; DFI < N; I++)
	{
		// Skip numbered vertices to cause the outerloop to find the
		// next DFS tree root in a disconnected graph
		if (gp_GetVertexParent(theGraph, I) != NIL)
		  continue;

		// DFS a connected component
		sp_Push2(theStack, NIL, NIL);
		while (sp_NonEmpty(theStack))
		{
			sp_Pop2(theStack, uparent, e);

			// For vertex uparent and edge e, obtain the opposing endpoint u of e
			// If uparent is NIL, then e is also NIL and we have encountered the
			// false edge to the DFS tree root as pushed above.
			u = uparent == NIL ? I : gp_GetNeighbor(theGraph, e);

			// We popped an edge to an unvisited vertex, so it is either a DFS tree edge
			// or a false edge to the DFS tree root (u).
			if (!gp_GetVertexVisited(theGraph, u))
			{
				gp_LogLine(gp_MakeLogStr3("v=%d, DFI=%d, parent=%d", u, DFI, uparent));

				// (1) Set the DFI and DFS parent
				gp_SetVertexVisited(theGraph, u);
				gp_SetVertexIndex(theGraph, u, DFI++);
				gp_SetVertexParent(theGraph, u, uparent);

				if (e != NIL)
				{
					// (2) Set the edge type values for tree edges
					gp_SetEdgeType(theGraph, e, EDGE_TYPE_CHILD);
					gp_SetEdgeType(theGraph, gp_GetTwinArc(theGraph, e), EDGE_TYPE_PARENT);

					// (3) Record u in the sortedDFSChildList of uparent
                    gp_SetVertexSortedDFSChildList(theGraph, uparent,
                    		LCAppend(theGraph->sortedDFSChildLists,
                    				 gp_GetVertexSortedDFSChildList(theGraph, uparent),
                    				 gp_GetVertexIndex(theGraph, u))
                    );

					// (7) Record e as the first and last arc of the virtual vertex
					//     at position DFI(u)+N, which is a root copy of uparent
                    R = gp_GetVertexIndex(theGraph, u) + N;
                	gp_SetFirstArc(theGraph, R, e);
                	gp_SetLastArc(theGraph, R, e);
				}

				// Push edges to all unvisited neighbors. These will be either
				// tree edges to children or forward arcs of back edges
				// Edges not pushed are marked as back edges here, except the
				// edge leading back to the immediate DFS parent.
				J = gp_GetFirstArc(theGraph, u);
				while (gp_IsArc(theGraph, J))
				{
					if (!gp_GetVertexVisited(theGraph, gp_GetNeighbor(theGraph, J)))
					{
						sp_Push2(theStack, u, J);
					}
					else if (gp_GetEdgeType(theGraph, J) != EDGE_TYPE_PARENT)
					{
						// (2) Set the edge type values for back edges
						gp_SetEdgeType(theGraph, J, EDGE_TYPE_BACK);
						JTwin = gp_GetTwinArc(theGraph, J);
						gp_SetEdgeType(theGraph, JTwin, EDGE_TYPE_FORWARD);

						// (4) Move the JTwin of back edge record J to the sortedFwdArcList of the ancestor
						uneighbor = gp_GetNeighbor(theGraph, J);
						JPrev = gp_GetPrevArc(theGraph, JTwin);
						JNext = gp_GetNextArc(theGraph, JTwin);

						if (gp_IsArc(theGraph, JPrev))
							 gp_SetNextArc(theGraph, JPrev, JNext);
						else gp_SetFirstArc(theGraph, uneighbor, JNext);
						if (gp_IsArc(theGraph, JNext))
							 gp_SetPrevArc(theGraph, JNext, JPrev);
						else gp_SetLastArc(theGraph, uneighbor, JPrev);

						e = gp_GetVertexFwdArcList(theGraph, uneighbor);
						if (gp_IsArc(theGraph, e))
						{
							JPrev = gp_GetPrevArc(theGraph, e);
							gp_SetPrevArc(theGraph, JTwin, JPrev);
							gp_SetNextArc(theGraph, JTwin, e);
							gp_SetPrevArc(theGraph, e, JTwin);
							gp_SetNextArc(theGraph, JPrev, JTwin);
						}
						else
						{
							gp_SetVertexFwdArcList(theGraph, uneighbor, JTwin);
							gp_SetPrevArc(theGraph, JTwin, JTwin);
							gp_SetNextArc(theGraph, JTwin, JTwin);
						}

						// (5) Update the leastAncestor value for the vertex u
						uneighbor = gp_GetVertexIndex(theGraph, uneighbor);
						if (uneighbor < gp_GetVertexLeastAncestor(theGraph, u))
							gp_SetVertexLeastAncestor(theGraph, u, uneighbor);
					}

					J = gp_GetNextArc(theGraph, J);
				}
			}
		}
	}

	// The graph is now DFS numbered
    theGraph->internalFlags |= FLAGS_DFSNUMBERED;

	// (6) Now that all vertices have a DFI in the index member, we can sort vertices
    if (gp_SortVertices(theGraph) != OK)
        return NOTOK;

    // Calculate the lowpoint values
    // (After the separatedDFSChildList construct is eliminated, this can be deferred to gp_Embed()
    //  where lowpoint calculation is not done until/unless needed)
/**/
    for (I = N-1; I >= 0; I--)
    {
    	leastValue = I;

        child = gp_GetVertexSortedDFSChildList(theGraph, I);
        while (child != NIL)
        {
        	if (leastValue > gp_GetVertexLowpoint(theGraph, child))
        		leastValue = gp_GetVertexLowpoint(theGraph, child);

            child = LCGetNext(theGraph->sortedDFSChildLists, gp_GetVertexSortedDFSChildList(theGraph, I), child);
        }

        if (leastValue > gp_GetVertexLeastAncestor(theGraph, I))
    		leastValue = gp_GetVertexLeastAncestor(theGraph, I);

    	gp_SetVertexLowpoint(theGraph, I, leastValue);
    }
/***/

	// (7) Create the DFS tree embedding using the child edge records stored in the virtual vertices
	// For each vertex I that is a DFS child, the virtual vertex R that will represent I's parent
	// in the singleton bicomp with I is at location I + N in the vertex array.
    for (I = 0, R = N; I < N; I++, R++)
    {
        if (gp_GetVertexParent(theGraph, I) == NIL)
        {
        	gp_SetFirstArc(theGraph, I, gp_AdjacencyListEndMark(I));
        	gp_SetLastArc(theGraph, I, gp_AdjacencyListEndMark(I));
        }
        else
        {
        	// Make the child edge the only edge in the virtual vertex adjacency list
        	J = gp_GetFirstArc(theGraph, R);
        	gp_SetPrevArc(theGraph, J, gp_AdjacencyListEndMark(I));
        	gp_SetNextArc(theGraph, J, gp_AdjacencyListEndMark(I));

        	// Reset the twin's neighbor value to point to the virtual vertex
        	JTwin = gp_GetTwinArc(theGraph, J);
        	gp_SetNeighbor(theGraph, JTwin, R);

        	// Make its twin the only edge in the child's adjacency list
        	gp_SetFirstArc(theGraph, I, JTwin);
        	gp_SetLastArc(theGraph, I, JTwin);
        	gp_SetPrevArc(theGraph, JTwin, gp_AdjacencyListEndMark(I));
        	gp_SetNextArc(theGraph, JTwin, gp_AdjacencyListEndMark(I));

        	// Set up the external face management data structure to match
            gp_SetExtFaceVertex(theGraph, R, 0, I);
            gp_SetExtFaceVertex(theGraph, R, 1, I);
            gp_SetExtFaceVertex(theGraph, I, 0, R);
            gp_SetExtFaceVertex(theGraph, I, 1, R);
        }
    }

	gp_LogLine("graphEmbed.c/_EmbeddingInitialize() end\n");

#ifdef PROFILE
platform_GetTime(end);
printf("Initialize embedding in %.3lf seconds.\n", platform_GetDuration(start,end));
#endif

	return OK;
}

/********************************************************************
 _CreateSortedSeparatedDFSChildLists()
 We create a separatedDFSChildList in each vertex to contain references
 to the DFS children vertices sorted in non-descending order by their
 Lowpoint values.
 To accomplish this in linear time for the whole graph, we must not
 sort the DFS children in each vertex, but rather bucket sort the
 Lowpoint values of all vertices, then traverse the buckets sequentially,
 adding each vertex to its parent's separatedDFSChildList.
 Note that this is a specialized bucket sort that achieves O(n)
 worst case rather than O(n) expected time due to the simplicity
 of the sorting problem.  Specifically, we know that the Lowpoint values
 are between 0 and N-1, so we create buckets for each value.
 Collisions occur only when two keys are equal, so there is no
 need to sort the buckets (hence O(n) worst case).
 ********************************************************************/

void  _CreateSortedSeparatedDFSChildLists(graphP theGraph)
{
int *buckets;
listCollectionP bin;
int I, L, N, DFSParent, theList;

     N = theGraph->N;
     buckets = theGraph->buckets;
     bin = theGraph->bin;

     /* Initialize the bin and all the buckets to be empty */

     LCReset(bin);
     for (I=0; I < N; I++)
          buckets[I] = NIL;

     /* For each vertex, add it to the bucket whose index is equal to
        the Lowpoint of the vertex. */

     for (I=0; I < N; I++)
     {
          L = gp_GetVertexLowpoint(theGraph, I);
          buckets[L] = LCAppend(bin, buckets[L], I);
     }

     /* For each bucket, add each vertex in the bucket to the
        separatedDFSChildList of its DFSParent.  Since lower numbered buckets
        are processed before higher numbered buckets, vertices with lower
        Lowpoint values are added before those with higher Lowpoint values,
        so the separatedDFSChildList of each vertex is sorted by Lowpoint */

     for (I = 0; I < N; I++)
     {
          if ((L=buckets[I]) != NIL)
          {
              while (L != NIL)
              {
                  DFSParent = gp_GetVertexParent(theGraph, L);

                  if (DFSParent != NIL && DFSParent != L)
                  {
                      theList = gp_GetVertexSeparatedDFSChildList(theGraph, DFSParent);
                      theList = LCAppend(theGraph->DFSChildLists, theList, L);
                      gp_SetVertexSeparatedDFSChildList(theGraph, DFSParent, theList);
                  }

                  L = LCGetNext(bin, buckets[I], L);
              }
          }
     }
}

/********************************************************************
 _CreateFwdArcLists()

 Puts the forward arcs (back edges from a vertex to its descendants)
 into a circular list indicated by the fwdArcList member, a task
 simplified by the fact that they have already been placed in
 succession at the end of the adjacency lists by gp_CreateDFSTree().

  Returns OK for success, NOTOK for internal code failure
 ********************************************************************/

int _CreateFwdArcLists(graphP theGraph)
{
int I, Jfirst, Jnext, Jlast;

    for (I=0; I < theGraph->N; I++)
    {
    	// The forward arcs are already in succession at the end of the adjacency list
    	// Skip this vertex if it has no edges

    	Jfirst = gp_GetLastArc(theGraph, I);
    	if (!gp_IsArc(theGraph, Jfirst))
    		continue;

        // If the vertex has any forward edges at all, then the last edge
    	// will be a forward edge.  So if we have any forward edges, ...

        if (gp_GetEdgeType(theGraph, Jfirst) == EDGE_TYPE_FORWARD)
        {
            // Find the end of the forward edge list

            Jnext = Jfirst;
            while (gp_GetEdgeType(theGraph, Jnext) == EDGE_TYPE_FORWARD)
                Jnext = gp_GetPrevArc(theGraph, Jnext);
            Jlast = gp_GetNextArc(theGraph, Jnext);

            // Remove the forward edges from the adjacency list of I
            gp_BindLastArc(theGraph, I, Jnext);

            // Make a circular forward edge list
            gp_SetVertexFwdArcList(theGraph, I, Jfirst);
            gp_SetNextArc(theGraph, Jfirst, Jlast);
            gp_SetPrevArc(theGraph, Jlast, Jfirst);
        }
    }

    return OK;
}

/********************************************************************
 _CreateDFSTreeEmbedding()

 Each vertex receives only its parent arc in the adjacency list, and
 the corresponding child arc is placed in the adjacency list of a root
 copy of the parent.  Each root copy of a vertex is uniquely associated
 with a child C, so it is simply stored at location C+N.

 The forward arcs are not lost because they are already in the
 fwdArcList of each vertex.  Each back arc can be reached as the
 twin arc of a forward arc, and the two are embedded together when
 the forward arc is processed.  Finally, the child arcs are initially
 placed in root copies of vertices, not the vertices themselves, but
 the child arcs are merged into the vertices as the embedder progresses.
 ********************************************************************/

void _CreateDFSTreeEmbedding(graphP theGraph)
{
int N, I, J, Jtwin, R;

    N = theGraph->N;

    // Embed all tree edges.  For each DFS tree child, we move
    // the child arc to a root copy of vertex I that is uniquely
    // associated with the DFS child, and we remove all edges
    // from the child except the parent arc

    for (I=0, R=N; I < N; I++, R++)
    {
        if (gp_GetVertexParent(theGraph, I) == NIL)
        {
        	gp_SetFirstArc(theGraph, I, gp_AdjacencyListEndMark(I));
        	gp_SetLastArc(theGraph, I, gp_AdjacencyListEndMark(I));
        }
        else
        {
            J = gp_GetFirstArc(theGraph, I);
            while (gp_GetEdgeType(theGraph, J) != EDGE_TYPE_PARENT)
                J = gp_GetNextArc(theGraph, J);

        	gp_SetFirstArc(theGraph, I, J);
        	gp_SetLastArc(theGraph, I, J);

        	gp_SetNextArc(theGraph, J, gp_AdjacencyListEndMark(I));
        	gp_SetPrevArc(theGraph, J, gp_AdjacencyListEndMark(I));

        	gp_SetNeighbor(theGraph, J, R);

            Jtwin = gp_GetTwinArc(theGraph, J);

        	gp_SetFirstArc(theGraph, R, Jtwin);
        	gp_SetLastArc(theGraph, R, Jtwin);

        	gp_SetNextArc(theGraph, Jtwin, gp_AdjacencyListEndMark(R));
        	gp_SetPrevArc(theGraph, Jtwin, gp_AdjacencyListEndMark(R));

            gp_SetExtFaceVertex(theGraph, R, 0, I);
            gp_SetExtFaceVertex(theGraph, R, 1, I);
            gp_SetExtFaceVertex(theGraph, I, 0, R);
            gp_SetExtFaceVertex(theGraph, I, 1, R);
        }
    }
}

/********************************************************************
 _EmbedBackEdgeToDescendant()
 The Walkdown has found a descendant vertex W to which it can
 attach a back edge up to the root of the bicomp it is processing.
 The RootSide and WPrevLink indicate the parts of the external face
 that will be replaced at each endpoint of the back edge.
 ********************************************************************/

void _EmbedBackEdgeToDescendant(graphP theGraph, int RootSide, int RootVertex, int W, int WPrevLink)
{
int fwdArc, backArc, parentCopy;

    /* We get the two edge records of the back edge to embed.
        The Walkup recorded in W's adjacentTo the index of the forward arc
        from the root's parent copy to the descendant W. */

    fwdArc = gp_GetVertexPertinentAdjacencyInfo(theGraph, W);
    backArc = gp_GetTwinArc(theGraph, fwdArc);

    /* The forward arc is removed from the fwdArcList of the root's parent copy. */

    parentCopy = gp_GetVertexParent(theGraph, RootVertex - theGraph->N);

    gp_LogLine(gp_MakeLogStr5("graphEmbed.c/_EmbedBackEdgeToDescendant() V=%d, R=%d, R_out=%d, W=%d, W_in=%d",
    		parentCopy, RootVertex, RootSide, W, WPrevLink));

    if (gp_GetVertexFwdArcList(theGraph, parentCopy) == fwdArc)
    {
    	gp_SetVertexFwdArcList(theGraph, parentCopy, gp_GetNextArc(theGraph, fwdArc));
        if (gp_GetVertexFwdArcList(theGraph, parentCopy) == fwdArc)
            gp_SetVertexFwdArcList(theGraph, parentCopy, NIL);
    }

    gp_SetNextArc(theGraph, gp_GetPrevArc(theGraph, fwdArc), gp_GetNextArc(theGraph, fwdArc));
    gp_SetPrevArc(theGraph, gp_GetNextArc(theGraph, fwdArc), gp_GetPrevArc(theGraph, fwdArc));

    // The forward arc is added to the adjacency list of the RootVertex.
    // Note that we're guaranteed that the RootVertex adjacency list is non-empty,
    // so tests for NIL are not needed
    gp_SetAdjacentArc(theGraph, fwdArc, 1^RootSide, gp_AdjacencyListEndMark(RootVertex));
    gp_SetAdjacentArc(theGraph, fwdArc, RootSide, gp_GetArc(theGraph, RootVertex, RootSide));
    gp_SetAdjacentArc(theGraph, gp_GetArc(theGraph, RootVertex, RootSide), 1^RootSide, fwdArc);
    gp_SetArc(theGraph, RootVertex, RootSide, fwdArc);

    // The back arc is added to the adjacency list of W.
    // The adjacency list of W is also guaranteed non-empty
    gp_SetAdjacentArc(theGraph, backArc, 1^WPrevLink, gp_AdjacencyListEndMark(W));
    gp_SetAdjacentArc(theGraph, backArc, WPrevLink, gp_GetArc(theGraph, W, WPrevLink));
    gp_SetAdjacentArc(theGraph, gp_GetArc(theGraph, W, WPrevLink), 1^WPrevLink, backArc);
    gp_SetArc(theGraph, W, WPrevLink, backArc);

    gp_SetNeighbor(theGraph, backArc, RootVertex);

    /* Link the two endpoint vertices together on the external face */

    gp_SetExtFaceVertex(theGraph, RootVertex, RootSide, W);
    gp_SetExtFaceVertex(theGraph, W, WPrevLink, RootVertex);
}

/********************************************************************
 _GetNextVertexOnExternalFace()
 Each vertex contains two 'link' index pointers that indicate the
 first and last adjacency list arc.  If the vertex is on the external face,
 then these two arcs are also on the external face.  We want to take one of
 those edges to get to the next vertex on the external face.
 On input *pPrevLink indicates which link we followed to arrive at
 curVertex.  On output *pPrevLink will be set to the link we follow to
 get into the next vertex.
 To get to the next vertex, we use the opposite link from the one used
 to get into curVertex.  This takes us to an edge node.  The twinArc
 of that edge node, carries us to an edge node in the next vertex.
 At least one of the two links in that edge node will lead to a vertex
 node in G, which is the next vertex.  Once we arrive at the next
 vertex, at least one of its links will lead back to the edge node, and
 that link becomes the output value of *pPrevLink.

 NOTE: This method intentionally ignores the extFace optimization
       links. It is invoked when the "real" external face must be
       traversed and hence when the constant time guarantee is not
       needed from the extFace short-circuit that connects the
       bicomp root to the first active vertices along each external
       face path emanating from the bicomp root.
 ********************************************************************/

int  _GetNextVertexOnExternalFace(graphP theGraph, int curVertex, int *pPrevLink)
{
     /* Exit curVertex from whichever link was not previously used to enter it */

     int arc = gp_GetArc(theGraph, curVertex, 1^(*pPrevLink));
     int nextVertex = gp_GetNeighbor(theGraph, arc);

     /* This if stmt assigns the new prev link that tells us which edge
        record was used to enter nextVertex (so that we exit from the
        opposing edge record).

        However, if we are in a singleton bicomp, then both links in nextVertex
        lead back to curVertex.  We want the two arcs of a singleton bicomp to
        act like a cycle, so we just don't change the prev link in this case.

        But when nextVertex has more than one edge, we need to figure out
        whether the first edge or last edge (which are the two on the external
        face) was used to enter nextVertex so we can exit from the other one
        as traversal of the external face continues later. */

     if (gp_GetFirstArc(theGraph, nextVertex) != gp_GetLastArc(theGraph, nextVertex))
         *pPrevLink = gp_GetTwinArc(theGraph, arc) == gp_GetFirstArc(theGraph, nextVertex) ? 0 : 1;

     return nextVertex;
}

/********************************************************************
 _InvertVertex()
 This function flips the orientation of a single vertex such that
 instead of using link successors to go clockwise (or counterclockwise)
 around a vertex's adjacency list, link predecessors would be used.
 ********************************************************************/

void _InvertVertex(graphP theGraph, int V)
{
int J, temp;

	 gp_LogLine(gp_MakeLogStr1("graphEmbed.c/_InvertVertex() V=%d", V));

     // Swap the links in all the arcs of the adjacency list
     J = gp_GetFirstArc(theGraph, V);
     while (gp_IsArc(theGraph, J))
     {
    	 temp = gp_GetNextArc(theGraph, J);
    	 gp_SetNextArc(theGraph, J, gp_GetPrevArc(theGraph, J));
    	 gp_SetPrevArc(theGraph, J, temp);

         J = temp;
     }

     // Swap the first/last edge record indicators in the vertex
     temp = gp_GetFirstArc(theGraph, V);
     gp_SetFirstArc(theGraph, V, gp_GetLastArc(theGraph, V));
     gp_SetLastArc(theGraph, V, temp);

     // Swap the first/last external face indicators in the vertex
     temp = gp_GetExtFaceVertex(theGraph, V, 0);
     gp_SetExtFaceVertex(theGraph, V, 0, gp_GetExtFaceVertex(theGraph, V, 1));
     gp_SetExtFaceVertex(theGraph, V, 1, temp);
}

/********************************************************************
 _MergeVertex()
 The merge step joins the vertex W to the root R of a child bicompRoot,
 which is a root copy of W appearing in the region N to 2N-1.

 Actually, the first step of this is to redirect all of the edges leading
 into R so that they indicate W as the neighbor instead of R.
 For each edge node pointing to R, we set the 'v' field to W.  Once an
 edge is redirected from a root copy R to a parent copy W, the edge is
 never redirected again, so we associate the cost of the redirection
 as constant per edge, which maintains linear time performance.

 After this is done, a regular circular list union occurs. The only
 consideration is that WPrevLink is used to indicate the two edge
 records e_w and e_r that will become consecutive in the resulting
 adjacency list of W.  We set e_w to W's link [WPrevLink] and e_r to
 R's link [1^WPrevLink] so that e_w and e_r indicate W and R with
 opposing links, which become free to be cross-linked.  Finally,
 the edge record e_ext, set equal to R's link [WPrevLink], is the edge
 that, with e_r, held R to the external face.  Now, e_ext will be the
 new link [WPrevLink] edge record for W.  If e_w and e_r become part
 of a proper face, then e_ext and W's link [1^WPrevLink] are the two
 edges that attach W to the external face cycle of the containing bicomp.
 ********************************************************************/

void _MergeVertex(graphP theGraph, int W, int WPrevLink, int R)
{
int  J, JTwin;
int  e_w, e_r, e_ext;

	 gp_LogLine(gp_MakeLogStr4("graphEmbed.c/_MergeVertex() W=%d, W_in=%d, R=%d, R_out=%d",
			 W, WPrevLink, R, 1^WPrevLink));

     // All arcs leading into R from its neighbors must be changed
     // to say that they are leading into W
     J = gp_GetFirstArc(theGraph, R);
     while (gp_IsArc(theGraph, J))
     {
         JTwin = gp_GetTwinArc(theGraph, J);
         gp_GetNeighbor(theGraph, JTwin) = W;

    	 J = gp_GetNextArc(theGraph, J);
     }

     // Obtain the edge records involved in the list union
     e_w = gp_GetArc(theGraph, W, WPrevLink);
     e_r = gp_GetArc(theGraph, R, 1^WPrevLink);
     e_ext = gp_GetArc(theGraph, R, WPrevLink);

     // If W has any edges, then join the list with that of R
     if (gp_IsArc(theGraph, e_w))
     {
         // The WPrevLink arc of W is e_w, so the 1^WPrevLink arc in e_w leads back to W.
         // Now it must lead to e_r.  Likewise, e_r needs to lead back to e_w with the
         // opposing link, which is WPrevLink
         // Note that the adjacency lists of W and R are guaranteed non-empty, which is
         // why these linkages can be made without NIL tests.
         gp_SetAdjacentArc(theGraph, e_w, 1^WPrevLink, e_r);
         gp_SetAdjacentArc(theGraph, e_r, WPrevLink, e_w);

         // Cross-link W's WPrevLink arc and the 1^WPrevLink arc in e_ext
         gp_SetArc(theGraph, W, WPrevLink, e_ext);
         gp_SetAdjacentArc(theGraph, e_ext, 1^WPrevLink, gp_AdjacencyListEndMark(W));
     }
     // Otherwise, W just receives R's list.  This happens, for example, on a
     // DFS tree root vertex during JoinBicomps()
     else
     {
         // Cross-link W's 1^WPrevLink arc and the WPrevLink arc in e_r
         gp_SetArc(theGraph, W, 1^WPrevLink, e_r);
         gp_SetAdjacentArc(theGraph, e_r, WPrevLink, gp_AdjacencyListEndMark(W));

         // Cross-link W's WPrevLink arc and the 1^WPrevLink arc in e_ext
         gp_SetArc(theGraph, W, WPrevLink, e_ext);
         gp_SetAdjacentArc(theGraph, e_ext, 1^WPrevLink, gp_AdjacencyListEndMark(W));
     }

     // Erase the entries in R, which is a root copy that is no longer needed
     theGraph->functions.fpInitVertexRec(theGraph, R);
}

/********************************************************************
 _MergeBicomps()

 Merges all biconnected components at the cut vertices indicated by
 entries on the stack.

 theGraph contains the stack of bicomp roots and cut vertices to merge

 I, RootVertex, W and WPrevLink are not used in this routine, but are
          used by overload extensions

 Returns OK, but an extension function may return a value other than
         OK in order to cause Walkdown to terminate immediately.
********************************************************************/

int  _MergeBicomps(graphP theGraph, int I, int RootVertex, int W, int WPrevLink)
{
int  R, Rout, Z, ZPrevLink, J;
int  theList, RootID_DFSChild;
int  extFaceVertex;

     while (sp_NonEmpty(theGraph->theStack))
     {
         sp_Pop2(theGraph->theStack, R, Rout);
         sp_Pop2(theGraph->theStack, Z, ZPrevLink);

         /* The external faces of the bicomps containing R and Z will
            form two corners at Z.  One corner will become part of the
            internal face formed by adding the new back edge. The other
            corner will be the new external face corner at Z.
            We first want to update the links at Z to reflect this. */

         extFaceVertex = gp_GetExtFaceVertex(theGraph, R, 1^Rout);
         gp_SetExtFaceVertex(theGraph, Z, ZPrevLink, extFaceVertex);

         if (gp_GetExtFaceVertex(theGraph, extFaceVertex, 0) == gp_GetExtFaceVertex(theGraph, extFaceVertex, 1))
            gp_SetExtFaceVertex(theGraph, extFaceVertex, Rout ^ gp_GetExtFaceInversionFlag(theGraph, extFaceVertex), Z);
         else
            gp_SetExtFaceVertex(theGraph, extFaceVertex, gp_GetExtFaceVertex(theGraph, extFaceVertex, 0) == R ? 0 : 1, Z);

         /* If the path used to enter Z is opposed to the path
            used to exit R, then we have to flip the bicomp
            rooted at R, which we signify by inverting R
            then setting the sign on its DFS child edge to
            indicate that its descendants must be flipped later */

         if (ZPrevLink == Rout)
         {
             Rout = 1^ZPrevLink;

             if (gp_GetFirstArc(theGraph, R) != gp_GetLastArc(theGraph, R))
                _InvertVertex(theGraph, R);

             J = gp_GetFirstArc(theGraph, R);
             while (gp_IsArc(theGraph, J))
             {
                 if (gp_GetEdgeType(theGraph, J) == EDGE_TYPE_CHILD)
                 {
                	 // The core planarity algorithm could simply "set" the inverted flag
                	 // because a bicomp root edge cannot be already inverted in the core
                	 // planarity algorithm at the time of this merge.
                	 // However, extensions may perform edge reductions on tree edges, resulting
                	 // in an inversion sign being promoted to the root edge of a bicomp before
                	 // it gets merged.  So, now we use xor to reverse the inversion flag on the
                	 // root edge if the bicomp root must be inverted before it is merged.
                	 gp_XorEdgeFlagInverted(theGraph, J);
                     break;
                 }

                 J = gp_GetNextArc(theGraph, J);
             }
         }

         // The endpoints of a bicomp's "root edge" are the bicomp root R and a
         // DFS child of the parent copy of the bicomp root R.
         // The locations of bicomp root (virtual) vertices is in the range N to 2N-1
         // at the offset indicated by the associated DFS child.  So, the location
         // of the root vertex R, less N, is the location of the DFS child and also
         // a convenient identifier for the bicomp root.
         RootID_DFSChild = R - theGraph->N;

         /* R is no longer pertinent to Z since we are about to
            merge R into Z, so we delete R from its pertinent
            bicomp list (Walkdown gets R from the head of the list). */

         theList = gp_GetVertexPertinentBicompList(theGraph, Z);
         theList = LCDelete(theGraph->BicompLists, theList, RootID_DFSChild);
         gp_SetVertexPertinentBicompList(theGraph, Z, theList);

         /* As a result of the merge, the DFS child of Z must be removed
            from Z's SeparatedDFSChildList because the child has just
            been joined directly to Z, rather than being separated by a
            root copy. */

         theList = gp_GetVertexSeparatedDFSChildList(theGraph, Z);
         theList = LCDelete(theGraph->DFSChildLists, theList, RootID_DFSChild);
         gp_SetVertexSeparatedDFSChildList(theGraph, Z, theList);

         /* Now we push R into Z, eliminating R */

         _MergeVertex(theGraph, Z, ZPrevLink, R);
     }

     return OK;
}

/********************************************************************
 _WalkUp()
 I is the vertex currently being embedded
 J is the forward arc to the descendant W on which the Walkup begins

 The Walkup establishes pertinence for step I.  It marks W with J
 as a way of indicating it is pertinent because it should be made
 'adjacent to' I by adding a back edge (I', W), which will occur when
 the Walkdown encounters W.

 The Walkup also determines the pertinent child bicomps that should be
 set up as a result of the need to embed edge (I, W). It does this by
 recording the pertinent child biconnected components of all cut
 vertices between W and the child of I that is an ancestor of W.
 Note that it stops the traversal if it finds a visited info value set
 to I, which indicates that a prior walkup call in step I has already
 done the work. This ensures work is not duplicated.

 A second technique used to maintain a total linear time bound for the
 whole planarity method is that of parallel external face traversal.
 This ensures that the cost of determining pertinence in step I is
 linearly commensurate with the length of the path that ultimately
 is removed from the external face.

 Zig and Zag are so named because one goes around one side of a bicomp
 and the other goes around the other side, yet we have as yet no notion
 of orientation for the bicomp. The edge record J from vertex I gestures
 to a descendant vertex W in some other bicomp.  Zig and Zag start out
 at W. They go around alternate sides of the bicomp until its root is
 found.  We then hop from the root copy to the parent copy of the vertex
 in order to record which bicomp we just came from and also to continue
 the walk-up at the parent copy as if it were the new W.  We reiterate
 this process until the parent copy actually is I, at which point the
 Walkup is done.
 ********************************************************************/

void _WalkUp(graphP theGraph, int I, int J)
{
int  N = theGraph->N, W = gp_GetNeighbor(theGraph, J);
int  Zig=W, Zag=W, ZigPrevLink=1, ZagPrevLink=0;
int  nextZig, nextZag, R, ParentCopy, RootID_DFSChild, BicompList;

	 // Start by marking W as being pertinent
     gp_SetVertexPertinentAdjacencyInfo(theGraph, W, J);

     // Zig and Zag are initialized at W, and we continue looping
     // around the external faces of bicomps up from W until we
     // reach vertex I (or until the visited info optimization
     // breaks the loop)
     while (Zig != I)
     {
    	 // Obtain the next vertex in a first direction and determine if it is a bicomp root
         if ((nextZig = gp_GetExtFaceVertex(theGraph, Zig, 1^ZigPrevLink)) >= N)
         {
        	 // If the current vertex along the external face was visited in this step I,
        	 // then the bicomp root and its ancestor roots have already been added.
        	 if (gp_GetVertexVisitedInfo(theGraph, Zig) == I) break;

        	 // Store the bicomp root that was found
        	 R = nextZig;

        	 // Since the bicomp root was the next vertex on the path from Zig, determine the
        	 // vertex on the opposing path that enters the bicomp root.
        	 nextZag = gp_GetExtFaceVertex(theGraph, R,
										   gp_GetExtFaceVertex(theGraph, R, 0)==Zig ? 1 : 0);

        	 // If the opposing vertex was already marked visited in this step, then a prior
        	 // Walkup already recorded as pertinent the bicomp root and its ancestor roots.
        	 if (gp_GetVertexVisitedInfo(theGraph, nextZag) == I) break;
         }

         // Obtain the next vertex in the parallel direction and perform the analogous logic
         else if ((nextZag = gp_GetExtFaceVertex(theGraph, Zag, 1^ZagPrevLink)) >= N)
         {
        	 if (gp_GetVertexVisitedInfo(theGraph, Zag) == I) break;
        	 R = nextZag;
        	 nextZig = gp_GetExtFaceVertex(theGraph, R,
										   gp_GetExtFaceVertex(theGraph, R, 0)==Zag ? 1 : 0);
        	 if (gp_GetVertexVisitedInfo(theGraph, nextZig) == I) break;
         }

         // The bicomp root was not found in either direction.
         else
         {
        	 if (gp_GetVertexVisitedInfo(theGraph, Zig) == I) break;
        	 if (gp_GetVertexVisitedInfo(theGraph, Zag) == I) break;
        	 R = NIL;
         }

         // This Walkup has now finished with another vertex along each of the parallel
         // paths, so they are marked visited in step I so that future Walkups in this
         // step I can break if these vertices are encountered again.
         gp_SetVertexVisitedInfo(theGraph, Zig, I);
         gp_SetVertexVisitedInfo(theGraph, Zag, I);

         // If both directions found new non-root vertices, then proceed with parallel external face traversal
         if (R == NIL)
         {
             ZigPrevLink = gp_GetExtFaceVertex(theGraph, nextZig, 0)==Zig ? 0 : 1;
             Zig = nextZig;

             ZagPrevLink = gp_GetExtFaceVertex(theGraph, nextZag, 0)==Zag ? 0 : 1;
             Zag = nextZag;
         }

         // The bicomp root was found and not previously recorded as pertinent,
         // so walk up to the parent bicomp and continue
         else
         {
             // The endpoints of a bicomp's "root edge" are the bicomp root R and a
             // DFS child of the parent copy of the bicomp root R.
             // The locations of bicomp root (virtual) vertices is in the range N to 2N-1
             // at the offset indicated by the associated DFS child.  So, the location
             // of the root vertex R, less N, is the location of the DFS child and also
             // a convenient identifier for the bicomp root.
             RootID_DFSChild = R - N;

             // Get the BicompList of the parent copy vertex.
             ParentCopy = gp_GetVertexParent(theGraph, RootID_DFSChild);
             BicompList = gp_GetVertexPertinentBicompList(theGraph, ParentCopy);

			 // Put the new root vertex in the BicompList.  It is prepended if internally
			 // active and appended if externally active so that all internally active
			 // bicomps are processed before any externally active bicomps by virtue of storage.

			 // NOTE: Unlike vertices, the activity status of a bicomp is computed solely
			 //       using lowpoint. The lowpoint of the DFS child in the bicomp's root edge
			 //	     indicates whether the DFS child or any of its descendants are joined by
			 //	     a back edge to ancestors of I. If so, then the bicomp rooted at
			 //	     RootVertex must contain an externally active vertex so the bicomp must
			 //	     be kept on the external face.
			 if (gp_GetVertexLowpoint(theGraph, RootID_DFSChild) < I)
			      BicompList = LCAppend(theGraph->BicompLists, BicompList, RootID_DFSChild);
			 else BicompList = LCPrepend(theGraph->BicompLists, BicompList, RootID_DFSChild);

			 // The head node of the parent copy vertex's bicomp list may have changed, so
			 // we assign the head of the modified list as the vertex's pertinent bicomp list
			 gp_SetVertexPertinentBicompList(theGraph, ParentCopy, BicompList);

             Zig = Zag = ParentCopy;
             ZigPrevLink = 1;
             ZagPrevLink = 0;
         }
     }
}

/********************************************************************
 _HandleBlockedDescendantBicomp()
 The core planarity/outerplanarity algorithm handles the blockage
 by pushing the root of the blocked bicomp onto the top of the stack
 because it is the central focus for obstruction minor A.
 Then NONEMBEDDABLE is returned so that the WalkDown can terminate,
 and the embedder can proceed to isolate the obstruction.
 Some algorithms may be able to clear the blockage, in which case
 a function overload would set Rout, W and WPrevLink, then return OK
 to indicate that the WalkDown may proceed.

 NOTE: When returning OK (blockage cleared), the overload implementation
       should NOT call this base implementation nor otherwise push R
       onto the stack because the core WalkDown implementation will push
       the appropriate stack entries based on R, Rout, W and WPrevLink
       Similarly, when returning NONEMBEDDABLE, it is typically not
       necessary to call this base implementation because pushing
       the bicomp root R is not usually necessary, i.e. the overload
       implementation usually does all embed post-processing before
       returning NONEMBEDDABLE.

 Returns OK to proceed with WalkDown at W,
         NONEMBEDDABLE to terminate WalkDown of Root Vertex
         NOTOK for internal error
 ********************************************************************/

int  _HandleBlockedDescendantBicomp(graphP theGraph, int I, int RootVertex, int R, int *pRout, int *pW, int *pWPrevLink)
{
    sp_Push2(theGraph->theStack, R, 0);
	return NONEMBEDDABLE;
}

/********************************************************************
 _HandleInactiveVertex()
 ********************************************************************/

int  _HandleInactiveVertex(graphP theGraph, int BicompRoot, int *pW, int *pWPrevLink)
{
     int X = gp_GetExtFaceVertex(theGraph, *pW, 1^*pWPrevLink);
     *pWPrevLink = gp_GetExtFaceVertex(theGraph, X, 0) == *pW ? 0 : 1;
     *pW = X;

     return OK;
}

/********************************************************************
 _GetPertinentChildBicomp()
 Returns the root of a pertinent child bicomp for the given vertex.
 Note: internally active roots are prepended by _Walkup()
 ********************************************************************/

#define _GetPertinentChildBicomp(theGraph, W) \
        (gp_GetVertexPertinentBicompList(theGraph, W)==NIL \
         ? NIL \
         : gp_GetVertexPertinentBicompList(theGraph, W) + theGraph->N)

/********************************************************************
 _WalkDown()
 Consider a circular shape with small circles and squares along its perimeter.
 The small circle at the top the root vertex of the bicomp.  The other small
 circles represent internally active vertices, and the squares represent
 externally active vertices.  The root vertex is a root copy of I, the
 vertex currently being processed.

 The Walkup previously marked all vertices adjacent to I by setting their
 adjacentTo flags.  Basically, we want to walk down both external face
 paths emanating from RootVertex, embedding edges between the RootVertex
 (a root copy of vertex I) and descendants of vertex I that have the
 adjacentTo flag set.

 During each walk down, it is sometimes necessary to hop from a vertex
 to one of its child biconnected components in order to reach the desired
 vertices.  In such cases, the biconnected components are merged together
 such that adding the back edge forms a new proper face in the biconnected
 component rooted at RootVertex (which, again, is a root copy of I).

 The outer loop performs both walks, unless the first walk got all the way
 around to RootVertex (only happens when bicomp contains no external activity,
 such as when processing the last vertex), or when non-planarity is
 discovered (in a pertinent child bicomp such that the stack is non-empty).

 For the inner loop, each iteration visits a vertex W.  If W is adjacentTo I,
 we call MergeBicomps to merge the biconnected components whose cut vertices
 have been collecting in theStack.  Then, we add the back edge (RootVertex, W)
 and clear the adjacentTo flag in W.

 Next, we check whether W has a pertinent child bicomp.  If so, then we figure
 out which path down from the root of the child bicomp leads to the next vertex
 to be visited, and we push onto the stack information on the cut vertex and
 the paths used to enter into it and exit from it.  Alternately, if W
 had no pertinent child bicomps, then we check to see if it is inactive.
 If so, we find the next vertex along the external face, then short-circuit
 its inactive predecessor (under certain conditions).  Finally, if W is not
 inactive, but it has no pertinent child bicomps, then we already know its
 adjacentTo flag is clear so both criteria for internal activity also fail.
 Therefore, W must be a stopping vertex.

 A stopping vertex X is an externally active vertex that has no pertinent
 child bicomps and no unembedded back edge to the current vertex I.
 The inner loop of Walkdown stops walking when it reaches a stopping vertex X
 because if it were to proceed beyond X and embed a back edge, then X would be
 surrounded by the bounding cycle of the bicomp.  This is clearly incorrect
 because X has a path leading from it to an ancestor of I (which is why it's
 externally active), and this path would have to cross the bounding cycle.

 After the loop, if the stack is non-empty, then the Walkdown halted because
 it could not proceed down a pertinent child biconnected component along either
 path from its root, which is easily shown to be evidence of a K_3,3, so
 we break the outer loop.  The caller performs further tests to determine
 whether Walkdown has embedded all back edges.  If the caller does not embed
 all back edges to descendants of the root vertex after walking both RootSide
 0 then 1 in all bicomps containing a root copy of I, then the caller can
 conclude that the input graph is non-planar.

  Returns OK if all possible edges were embedded, NONEMBEDDABLE if less
          than all possible edges were embedded, and NOTOK for an internal
          code failure
 ********************************************************************/

int  _WalkDown(graphP theGraph, int I, int RootVertex)
{
int  RetVal, W, WPrevLink, R, Rout, X, XPrevLink, Y, YPrevLink, RootSide, RootEdgeChild;

#ifdef DEBUG
     // Resolves typical watch expressions
     R = RootVertex;
#endif

     RootEdgeChild = RootVertex - theGraph->N;

     sp_ClearStack(theGraph->theStack);

     for (RootSide = 0; RootSide < 2; RootSide++)
     {
         W = gp_GetExtFaceVertex(theGraph, RootVertex, RootSide);

         // If the main bicomp rooted by RootVertex is a single tree edge,
         // (always the case for core planarity) then the external face links
         // of W will be equal
         if (gp_GetExtFaceVertex(theGraph, W, 0) == gp_GetExtFaceVertex(theGraph, W, 1))
         {
        	 // In this case, we treat the bicomp external face as if it were
        	 // a cycle of two edges and as if RootVertex and W had the same
        	 // orientation. Thus, the edge record leading back to RootVertex
        	 // would be indicated by link[1^RootSide] as this is the reverse of
        	 // link[RootSide], which was used to exit RootVertex and get to W
             WPrevLink = 1^RootSide;
             // We don't bother with the inversionFlag here because WalkDown is
             // never called on a singleton bicomp with an inverted orientation
             // Before the first Walkdown, the bicomp truly is a single edge
             // with proper orientation, and an extension algorithm does call
             // Walkdown again in post-processing, it wouldn't do so on this
             // bicomp because a singleton, whether inverted or not, would no
             // longer be pertinent (until a future vertex step).
             // Thus only the inner loop below accommodates the inversionFlag
             // when it walks down to a *pertinent* child biconnected component
             //WPrevLink = gp_GetExtFaceInversionFlag(theGraph, W) ? RootSide : 1^RootSide;
         }
         // Otherwise, Walkdown has been called on a bicomp with two distinct
         // external face paths from RootVertex (a possibility in extension
         // algorithms), so both external face path links from W do not indicate
         // the RootVertex.
         else
         {
        	 WPrevLink = gp_GetExtFaceVertex(theGraph, W, 0) == RootVertex ? 0 : 1;
        	 if (gp_GetExtFaceVertex(theGraph, W, WPrevLink) != RootVertex)
        		 return NOTOK;
         }

         while (W != RootVertex)
         {
             /* If the vertex W is the descendant endpoint of an unembedded
                back edge to I, then ... */

             if (gp_GetVertexPertinentAdjacencyInfo(theGraph, W) != NIL)
             {
                /* Merge bicomps at cut vertices on theStack and add the back edge,
                    creating a new proper face. */

                if (sp_NonEmpty(theGraph->theStack))
                {
                    if ((RetVal = theGraph->functions.fpMergeBicomps(theGraph, I, RootVertex, W, WPrevLink)) != OK)
                        return RetVal;
                }
                theGraph->functions.fpEmbedBackEdgeToDescendant(theGraph, RootSide, RootVertex, W, WPrevLink);

                /* Clear W's AdjacentTo flag so we don't add another edge to W if
                    this invocation of Walkdown visits W again later (and more
                    generally, so that no more back edges to W are added until
                    a future Walkup sets the flag to non-NIL again). */

                gp_SetVertexPertinentAdjacencyInfo(theGraph, W, NIL);
             }

             /* If there is a pertinent child bicomp, then we need to push it onto the stack
                along with information about how we entered the cut vertex and how
                we exit the root copy to get to the next vertex. */

             if (gp_GetVertexPertinentBicompList(theGraph, W) != NIL)
             {
                 sp_Push2(theGraph->theStack, W, WPrevLink);
                 R = _GetPertinentChildBicomp(theGraph, W);

                 /* Get next active vertices X and Y on ext. face paths emanating from R */

                 X = gp_GetExtFaceVertex(theGraph, R, 0);
                 XPrevLink = gp_GetExtFaceVertex(theGraph, X, 1)==R ? 1 : 0;
                 Y = gp_GetExtFaceVertex(theGraph, R, 1);
                 YPrevLink = gp_GetExtFaceVertex(theGraph, Y, 0)==R ? 0 : 1;

                 /* If this is a bicomp with only two ext. face vertices, then
                    it could be that the orientation of the non-root vertex
                    doesn't match the orientation of the root due to our relaxed
                    orientation method. */

                 if (X == Y && gp_GetExtFaceInversionFlag(theGraph, X))
                 {
                     XPrevLink = 0;
                     YPrevLink = 1;
                 }

                 /* Now we implement the Walkdown's simple path selection rules!
                    If either X or Y is internally active (pertinent but not
                    externally active), then we pick it first.  Otherwise,
                    we choose a pertinent vertex. If neither are pertinent,
                    then we let a handler decide.  The default handler for
                    core planarity/outerplanarity decides to stop the WalkDown
                    with the current blocked bicomp at the top of the stack. */

                 if (_VertexActiveStatus(theGraph, X, I) == VAS_INTERNAL)
                 {
                      W = X;
                      WPrevLink = XPrevLink;
                      Rout = 0;
                 }
                 else if (_VertexActiveStatus(theGraph, Y, I) == VAS_INTERNAL)
                 {
                      W = Y;
                      WPrevLink = YPrevLink;
                      Rout = 1;
                 }
                 else if (PERTINENT(theGraph, X))
                 {
                      W = X;
                      WPrevLink = XPrevLink;
                      Rout = 0;
                 }
                 else if (PERTINENT(theGraph, Y))
                 {
                	 W = Y;
                     WPrevLink = YPrevLink;
                     Rout = 1;
                 }
                 else
                 {
                	 // Both the X and Y sides of the bicomp are blocked.
                	 // Let the application decide whether it can unblock the bicomp.
                	 // The core planarity embedder simply pushes (R, 0) onto the top of
                	 // the stack and returns NONEMBEDDABLE, which causes a return here
                	 // and enables isolation of planarity/outerplanary obstruction minor A
                     if ((RetVal = theGraph->functions.fpHandleBlockedDescendantBicomp(theGraph, I, RootVertex, R, &Rout, &W, &WPrevLink)) != OK)
                         return RetVal;
                 }

                 sp_Push2(theGraph->theStack, R, Rout);
             }

             /* Skip inactive vertices, which will be short-circuited
                later by our fast external face linking method (once
                upon a time, we added false edges called short-circuit
                edges to eliminate inactive vertices, but the extFace
                links can do the same job and also give us the ability
                to more quickly test planarity without creating an embedding). */

             else if (_VertexActiveStatus(theGraph, W, I) == VAS_INACTIVE)
             {
                 if (theGraph->functions.fpHandleInactiveVertex(theGraph, RootVertex, &W, &WPrevLink) != OK)
                     return NOTOK;
             }

             /* At this point, we know that W is not inactive, but its adjacentTo flag
                is clear, and it has no pertinent child bicomps.  Therefore, it
                is an externally active stopping vertex. */

             else break;
         }

         /* We short-circuit the external face of the bicomp by hooking the root
            to the terminating externally active vertex so that inactive vertices
            are not visited in future iterations.  This setting obviates the need
            for those short-circuit edges mentioned above.

            NOTE: We skip the step if the stack is non-empty since in that case
                    we did not actually merge the bicomps necessary to put
                    W and RootVertex into the same bicomp. */

         gp_SetExtFaceVertex(theGraph, RootVertex, RootSide, W);
         gp_SetExtFaceVertex(theGraph, W, WPrevLink, RootVertex);

         /* If the bicomp is reduced to having only two external face vertices
             (the root and W), then we need to record whether the orientation
             of W is inverted relative to the root.  This is used later when a
             future Walkdown descends to and merges the bicomp containing W.
             Going from the root to W, we only get the correct WPrevLink if
             we know whether or not W is inverted.
             NOTE: In the case where we have to clear the flag here, it is because
                 it may have been set in W if W previously became part of a bicomp
                 with only two ext. face vertices, but then was flipped and merged
                 into a larger bicomp that is now again becoming a bicomp with only
                 two ext. face vertices. */

         if (gp_GetExtFaceVertex(theGraph, W, 0) == gp_GetExtFaceVertex(theGraph, W, 1) &&
             WPrevLink == RootSide)
              gp_SetExtFaceInversionFlag(theGraph, W);
         else gp_ClearExtFaceInversionFlag(theGraph, W);

         /* If we got back around to the root, then all edges
            are embedded, so we stop. */

         if (W == RootVertex)
             break;
     }

     return OK;
}


/********************************************************************
 gp_Embed()

  First, a DFS tree is created in the graph (if not already done).
  Then, the graph is sorted by DFI.

  Either a planar embedding is created in theGraph, or a Kuratowski
  subgraph is isolated.  Either way, theGraph remains sorted by DFI
  since that is the most common desired result.  The original vertex
  numbers are available in the 'index' members of the vertex records.
  Moreover, gp_SortVertices() can be invoked to put the vertices in
  the order of the input graph, at which point the 'index' members of
  the vertex records will contain the vertex DFIs.

 return OK if the embedding was successfully created or no subgraph
            homeomorphic to a topological obstruction was found.

        NOTOK on internal failure

        NONEMBEDDABLE if the embedding couldn't be created due to
                the existence of a subgraph homeomorphic to a
                topological obstruction.

  For core planarity, OK is returned when theGraph contains a planar
  embedding of the input graph, and NONEMBEDDABLE is returned when a
  subgraph homeomorphic to K5 or K3,3 has been isolated in theGraph.

  Extension modules can overload functions used by gp_Embed to achieve
  alternate algorithms.  In those cases, the return results are
  similar.  For example, a K3,3 search algorithm would return
  NONEMBEDDABLE if it finds the K3,3 obstruction, and OK if the graph
  is planar or only contains K5 homeomorphs.  Similarly, an
  outerplanarity module can return OK for an outerplanar embedding or
  NONEMBEDDABLE when a subgraph homeomorphic to K2,3 or K4 has been
  isolated.

  The algorithm extension for gp_Embed() is encoded in the embedFlags,
  and the details of the return value can be found in the extension
  module that defines the embedding flag.

 ********************************************************************/

int gp_Embed(graphP theGraph, int embedFlags)
{
int N, I, J, child;
int RetVal = OK;

    // Basic parameter checks
    if (theGraph==NULL)
    	return NOTOK;

    // A little shorthand for the order of the graph
    N = theGraph->N;

    // Preprocessing
    theGraph->embedFlags = embedFlags;

    if (_EmbeddingInitialize(theGraph) != OK)
    	return NOTOK;

/*
    if (gp_PreprocessForEmbedding(theGraph) != OK)
          return NOTOK;

    if (!(theGraph->internalFlags & FLAGS_SORTEDBYDFI))
        if (gp_SortVertices(theGraph) != OK)
            return NOTOK;
**/
    _CreateSortedSeparatedDFSChildLists(theGraph);
/*
    if (theGraph->functions.fpCreateFwdArcLists(theGraph) != OK)
        return NOTOK;

    // Embed the DFS tree edges
    theGraph->functions.fpCreateDFSTreeEmbedding(theGraph);
**/
    // In reverse DFI order, embed the back edges from each vertex to its DFS descendants.
    // Vertex and visited info and lowpoint settings are made in step I so they are available
    // to ancestors of I. During processing of I, these values are needed for descendants
    // of I, which is guaranteed due to the reverse DFI processing order.
    for (I = N-1; I >= 0; I--)
    {
          RetVal = OK;

          // To help optimize pertinence determination when processing ancestors of I, each vertex
          // visited info is initially set to N. Any setting greater than I means unvisited in step I,
          // so all initialized vertices implicitly revert to unvisited in each step.
          gp_SetVertexVisitedInfo(theGraph, I, N);

          // Walkup calls establish Pertinence in Step I
          // Do the Walkup for each cycle edge from I to a DFS descendant W.
          J = gp_GetVertexFwdArcList(theGraph, I);
          while (J != NIL)
          {
        	  theGraph->functions.fpWalkUp(theGraph, I, J);

              J = gp_GetNextArc(theGraph, J);
              if (J == gp_GetVertexFwdArcList(theGraph, I))
                  J = NIL;
          }

          // Initialize the lowpoint of I to its least ancestor value.
///          gp_SetVertexLowpoint(theGraph, I, gp_GetVertexLeastAncestor(theGraph, I));

          // For each DFS child C of the current vertex,
          //	1) Reduce the lowpoint value of I to lowpoint(C) if it is the lesser
          //	2) If the child C is pertinent, then do a Walkdown to embed the back edges
          child = gp_GetVertexSortedDFSChildList(theGraph, I);
/*
          child = gp_GetVertexPertinentBicompList(theGraph, I);
**/
          while (child != NIL)
          {
///        	  if (gp_GetVertexLowpoint(theGraph, I) > gp_GetVertexLowpoint(theGraph, child))
///        		  gp_SetVertexLowpoint(theGraph, I, gp_GetVertexLowpoint(theGraph, child));

        	  if (gp_GetVertexPertinentBicompList(theGraph, child) != NIL)
        	  {
				  if ((RetVal = theGraph->functions.fpWalkDown(theGraph, I, child + N)) != OK)
				  {
					  // _Walkdown currently returns OK even if it couldn't embed all back edges
					  // from I to the subtree rooted by child C. It only returns NONEMBEDDABLE
					  // when it was blocked on a descendant bicomp.
					  // Some extension algorithms are able to clear some such blockages with a reduction,
					  // and those algorithms only return NONEMBEDDABLE when unable to clear the blockage
					  if (RetVal == NONEMBEDDABLE)
						  break;
					  else
						  return NOTOK;
				  }
        	  }

			  child = LCGetNext(theGraph->sortedDFSChildLists, gp_GetVertexSortedDFSChildList(theGraph, I), child);
/*
			  child = LCGetNext(theGraph->BicompLists, gp_GetVertexPertinentBicompList(theGraph, I), child);
**/
          }

          // To reduce condition tests in Walkup, it is allowed to record pertinent roots
          // of the current vertex I, which we clear here
          gp_SetVertexPertinentBicompList(theGraph, I, NIL);

          // If the Walkdown sequence is completed but not all forward edges are embedded or an
          // explicit NONEMBEDDABLE result was returned, then the graph is not planar/outerplanar.
          // The handler/ below is invoked because some extension algorithms are able to clear the
          // embedding blockage and continue the embedder iteration loop (they return OK below).
          // The default implementation simply returns NONEMBEDDABLE, which stops the embedding process.
          if (gp_GetVertexFwdArcList(theGraph, I) != NIL || RetVal == NONEMBEDDABLE)
          {
              RetVal = theGraph->functions.fpHandleBlockedEmbedIteration(theGraph, I);
              if (RetVal != OK)
                  break;
          }
    }

    // Postprocessing to orient the embedding and merge any remaining separated bicomps,
    // or to isolate an obstruction to planarity/outerplanarity.  Some extension algorithms
    // either do nothing if they have already isolated a subgraph of interest, or they may
    // do so now based on information collected by their implementations of
    // HandleBlockedDescendantBicomp or HandleBlockedEmbedIteration
    return theGraph->functions.fpEmbedPostprocess(theGraph, I, RetVal);
}

/********************************************************************
 HandleBlockedEmbedIteration()

  At the end of each embedding iteration, this function is invoked
  if there are any unembedded cycle edges from the current vertex I
  to its DFS descendants. Specifically, the forward arc list of I is
  non-empty at the end of the edge addition processing for I.

  We return NONEMBEDDABLE to cause iteration to stop because the
  graph is non-planar if any edges could not be embedded.

  Extensions may overload this function and decide to proceed with or
  halt embedding iteration for application-specific reasons.
  For example, a search for K_{3,3} homeomorphs could reduce an
  isolated K5 homeomorph to something that can be ignored, and then
  return OK in order to continue the planarity algorithm in order to
  search for a K_{3,3} homeomorph elsewhere in the graph.  On the
  other hand, if such an algorithm found a K_{3,3} homeomorph,
  perhaps alone or perhaps entangled with the K5 homeomorph, it would
  return NONEMBEDDABLE since there is no need to continue with
  embedding iterations once the desired embedding obstruction is found.

  If this function returns OK, then embedding will proceed to the
  next iteration, or return OK if it finished the last iteration.

  If this function returns NONEMBEDDABLE, then the embedder will
  stop iteration and return NONEMBEDDABLE.  Note that the function
  _EmbedPostprocess() is still called in this case, allowing for
  further processing of the non-embeddable result, e.g. isolation
  of the desired embedding obstruction.

  This function can return NOTOK to signify an internal error.
 ********************************************************************/

int  _HandleBlockedEmbedIteration(graphP theGraph, int I)
{
     return NONEMBEDDABLE;
}

/********************************************************************
 _EmbedPostprocess()

 After the loop that embeds the cycle edges from each vertex to its
 DFS descendants, this method is invoked to postprocess the graph.
 If the graph is planar, then a consistent orientation is imposed
 on the vertices of the embedding, and any remaining separated
 biconnected components are joined together.
 If the graph is non-planar, then a subgraph homeomorphic to K5
 or K3,3 is isolated.
 Extensions may override this function to provide alternate
 behavior.

  @param theGraph - the graph ready for postprocessing
  @param I - the last vertex processed by the edge embedding loop
  @param edgeEmbeddingResult -
         OK if all edge embedding iterations returned OK
         NONEMBEDDABLE if an embedding iteration failed to embed
             all edges for a vertex

  @return NOTOK on internal failure
          NONEMBEDDABLE if a subgraph homeomorphic to a topological
              obstruction is isolated in the graph
          OK otherwise (for example if the graph contains a
             planar embedding or if a desired topological obstruction
             was not found)

 *****************************************************************/

int  _EmbedPostprocess(graphP theGraph, int I, int edgeEmbeddingResult)
{
int  RetVal = edgeEmbeddingResult;

    /* If an embedding was found, then post-process the embedding structure
        to eliminate root copies and give a consistent orientation to all vertices. */

    if (edgeEmbeddingResult == OK)
    {
    	if (_OrientVerticesInEmbedding(theGraph) != OK ||
    		_JoinBicomps(theGraph) != OK)
    		RetVal = NOTOK;
    }

    /* If the graph was found to be unembeddable, then we want to isolate an
        obstruction.  But, if a search flag was set, then we have already
        found a subgraph with the desired structure, so no further work is done. */

    else if (edgeEmbeddingResult == NONEMBEDDABLE)
    {
        if (theGraph->embedFlags == EMBEDFLAGS_PLANAR)
        {
            if (_IsolateKuratowskiSubgraph(theGraph, I, NIL) != OK)
                RetVal = NOTOK;
        }
        else if (theGraph->embedFlags == EMBEDFLAGS_OUTERPLANAR)
        {
            if (_IsolateOuterplanarObstruction(theGraph, I, NIL) != OK)
                RetVal = NOTOK;
        }
    }

    return RetVal;
}

/********************************************************************
 _OrientVerticesInEmbedding()

 Each vertex will then have an orientation, either clockwise or
 counterclockwise.  All vertices in each bicomp need to have the
 same orientation.
 This method clears the stack, and the stack is clear when it
 is finished.
 Returns OK on success, NOTOK on implementation failure.
 ********************************************************************/

int  _OrientVerticesInEmbedding(graphP theGraph)
{
int  R, Vsize = theGraph->N + theGraph->NV;

     sp_ClearStack(theGraph->theStack);

/* Run the array of root copy vertices.  For each that is not defunct
        (i.e. has not been merged during embed), we orient the vertices
        in the bicomp for which it is the root vertex. */

     for (R = theGraph->N; R < Vsize; R++)
     {
          if (gp_IsArc(theGraph, gp_GetFirstArc(theGraph, R)))
          {
        	  if (_OrientVerticesInBicomp(theGraph, R, 0) != OK)
        		  return NOTOK;
          }
     }
     return OK;
}

/********************************************************************
 _OrientVerticesInBicomp()
  As a result of the work done so far, the edges around each vertex have
 been put in order, but the orientation may be counterclockwise or
 clockwise for different vertices within the same bicomp.
 We need to reverse the orientations of those vertices that are not
 oriented the same way as the root of the bicomp.

 During embedding, a bicomp with root edge (v', c) may need to be flipped.
 We do this by inverting the root copy v' and implicitly inverting the
 orientation of the vertices in the subtree rooted by c by assigning -1
 to the sign of the DFSCHILD edge record leading to c.

 We now use these signs to help propagate a consistent vertex orientation
 throughout all vertices that have been merged into the given bicomp.
 The bicomp root contains the orientation to be imposed on all parent
 copy vertices.  We perform a standard depth first search to visit each
 vertex.  A vertex must be inverted if the product of the edge signs
 along the tree edges between the bicomp root and the vertex is -1.

 Finally, the PreserveSigns flag, if set, performs the inversions
 but does not change any of the edge signs.  This allows a second
 invocation of this function to restore the state of the bicomp
 as it was before the first call.

 This method uses the stack but preserves whatever may have been
 on it.  In debug mode, it will return NOTOK if the stack overflows.
 This method pushes at most two integers per vertext in the bicomp.

 Returns OK on success, NOTOK on implementation failure.
 ********************************************************************/

int  _OrientVerticesInBicomp(graphP theGraph, int BicompRoot, int PreserveSigns)
{
int  V, J, invertedFlag;
int  stackBottom = sp_GetCurrentSize(theGraph->theStack);

     sp_Push2(theGraph->theStack, BicompRoot, 0);

     while (sp_GetCurrentSize(theGraph->theStack) > stackBottom)
     {
         /* Pop a vertex to orient */
         sp_Pop2(theGraph->theStack, V, invertedFlag);

         /* Invert the vertex if the inverted flag is set */
         if (invertedFlag)
             _InvertVertex(theGraph, V);

         /* Push the vertex's DFS children that are in the bicomp */
         J = gp_GetFirstArc(theGraph, V);
         while (gp_IsArc(theGraph, J))
         {
             if (gp_GetEdgeType(theGraph, J) == EDGE_TYPE_CHILD)
             {
                 sp_Push2(theGraph->theStack, gp_GetNeighbor(theGraph, J),
                		  invertedFlag ^ gp_GetEdgeFlagInverted(theGraph, J));

                 if (!PreserveSigns)
                	 gp_ClearEdgeFlagInverted(theGraph, J);
             }

             J = gp_GetNextArc(theGraph, J);
         }
     }
     return OK;
}

/********************************************************************
 _JoinBicomps()
 The embedding algorithm works by only joining bicomps once the result
 forms a larger bicomp.  However, if the original graph was separable
 or disconnected, then the result of the embed function will be a
 graph that contains each bicomp as a distinct entity.  The root of
 each bicomp will be in the region N to 2N-1.  This function merges
 the bicomps into one connected graph.
 ********************************************************************/

int  _JoinBicomps(graphP theGraph)
{
int  R, N, Vsize = theGraph->N + theGraph->NV;

     for (R=N=theGraph->N; R < Vsize; R++)
          if (gp_IsArc(theGraph, gp_GetFirstArc(theGraph, R)))
        	  _MergeVertex(theGraph, gp_GetVertexParent(theGraph, R-N), 0, R);

     return OK;
}

/****************************************************************************
 _OrientExternalFacePath()

 The vertices along the path (v ... w) are assumed to be degree two vertices
 in an external face path connecting u and x.  This method imparts the
 orientation of u and x onto the vertices v ... w.
 The work done is on the order of the path length.
 Returns OK if the external face path was oriented, NOTOK on implementation
 error (i.e. if a condition arises providing the path is not on the
 external face).
 ****************************************************************************/

int  _OrientExternalFacePath(graphP theGraph, int u, int v, int w, int x)
{
int  e_u, e_v, e_ulink, e_vlink;

    // Get the edge record in u that indicates v; uses the twinarc method to
    // ensure the cost is dominated by the degree of v (which is 2), not u
    // (which can be any degree).
    e_u = gp_GetTwinArc(theGraph, gp_GetNeighborEdgeRecord(theGraph, v, u));

    do {
        // Get the external face link in vertex u that indicates the
        // edge e_u which connects to the next vertex v in the path
    	// As a sanity check, we determine whether e_u is an
    	// external face edge, because there would be an internal
    	// implementation error if not
    	if (gp_GetFirstArc(theGraph, u) == e_u)
    		e_ulink = 0;
    	else if (gp_GetLastArc(theGraph, u) == e_u)
    		e_ulink = 1;
    	else return NOTOK;

        v = gp_GetNeighbor(theGraph, e_u);

        // Now get the external face link in vertex v that indicates the
        // edge e_v which connects back to the prior vertex u.
        e_v = gp_GetTwinArc(theGraph, e_u);

    	if (gp_GetFirstArc(theGraph, v) == e_v)
    		e_vlink = 0;
    	else if (gp_GetLastArc(theGraph, v) == e_v)
    		e_vlink = 1;
    	else return NOTOK;

        // The vertices u and v are inversely oriented if they
        // use the same link to indicate the edge [e_u, e_v].
        if (e_vlink == e_ulink)
        {
            _InvertVertex(theGraph, v);
            e_vlink = 1^e_vlink;
        }

        // This update of the extFace short-circuit is polite but unnecessary.
        // This orientation only occurs once we know we can isolate a K_{3,3},
        // at which point the extFace data structure is not used.
        gp_SetExtFaceVertex(theGraph, u, e_ulink, v);
        gp_SetExtFaceVertex(theGraph, v, e_vlink, u);

        u = v;
        e_u = gp_GetArc(theGraph, v, 1^e_vlink);
    } while (u != x);

    return OK;
}

