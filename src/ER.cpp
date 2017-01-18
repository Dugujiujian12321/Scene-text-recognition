﻿#include "ER.h"

// ====================================================
// ======================== ER ========================
// ====================================================
ER::ER(const int level_, const int pixel_, const int x_, const int y_) : level(level_), pixel(pixel_), area(1), done(false), stability(.0), 
																		parent(nullptr), child(nullptr), next(nullptr)
{
	bound = Rect(x_, y_, 1, 1);
#ifndef DO_OCR
	sibling_L = nullptr;
	sibling_R = nullptr;
#endif
}

// ====================================================
// ===================== ER_filter ====================
// ====================================================
ERFilter::ERFilter(int thresh_step, int min_area, int max_area, int stability_t, double overlap_coef) : THRESH_STEP(thresh_step), MIN_AREA(min_area), MAX_AREA(max_area),
																										STABILITY_T(stability_t), OVERLAP_COEF(overlap_coef)
{

}


inline void ERFilter::er_accumulate(ER *er, const int &current_pixel, const int &x, const int &y)
{
	er->area++;

	const int x1 = min(er->bound.x, x);
	const int x2 = max(er->bound.br().x - 1, x);
	const int y1 = min(er->bound.y, y);
	const int y2 = max(er->bound.br().y - 1, y);

	er->bound.x = x1;
	er->bound.y = y1;
	er->bound.width = x2 - x1 + 1;
	er->bound.height = y2 - y1 + 1;
}

void ERFilter::er_merge(ER *parent, ER *child)
{
	parent->area += child->area;

	const int x1 = min(parent->bound.x, child->bound.x);
	const int x2 = max(parent->bound.br().x - 1, child->bound.br().x - 1);
	const int y1 = min(parent->bound.y, child->bound.y);
	const int y2 = max(parent->bound.br().y - 1, child->bound.br().y - 1);

	parent->bound.x = x1;
	parent->bound.y = y1;
	parent->bound.width = x2 - x1 + 1;
	parent->bound.height = y2 - y1 + 1;

	if (child->area <= MIN_AREA ||
		child->bound.height <= 10 ||
		child->bound.width <= 5)
	{
		ER *new_child = child->child;

		if (new_child)
		{
			while (new_child->next)
				new_child = new_child->next;
			new_child->next = parent->child;
			parent->child = child->child;
			child->child->parent = parent;
		}
		delete child;
	}
	else
	{
		child->next = parent->child;
		parent->child = child;
		child->parent = parent;
	}
}


void ERFilter::er_save(ER *er)
{
	// Non Recursive Preorder Tree Traversal
	// See http://algorithms.tutorialhorizon.com/binary-tree-preorder-traversal-non-recursive-approach/ for more info.

	// 1. Create a Stack
	vector<ER *> tree_stack;
	ER *root = er;

save_step_2:
	// 2. Print the root and push it to Stack and go left, i.e root=root.left and till it hits the nullptr.
	for (; root != nullptr; root = root->child)
	{
		tree_stack.push_back(root);
	}

	// 3. If root is null and Stack is empty Then
	//		return, we are done.
	if (root == nullptr && tree_stack.empty())
	{
		return;
	}

	// 4. Else
	//		Pop the top Node from the Stack and set it as, root = popped_Node.
	//		Go right, root = root.right.
	//		Go to step 2.

	root = tree_stack.back();
	tree_stack.pop_back();
	root = root->next;
	goto save_step_2;

	// 5. End If
}


// extract the component tree and store all the ER regions
// base on OpenCV source code, see https://github.com/Itseez/opencv_contrib/tree/master/modules/text for more info
// uses the algorithm described in 
// Linear time maximally stable extremal regions, D Nistér, H Stewénius – ECCV 2008
ER* ERFilter::er_tree_extract(Mat input)
{
	CV_Assert(input.type() == CV_8UC1);

	Mat input_clone = input.clone();
	const int width = input_clone.cols;
	const int height = input_clone.rows;
	const int highest_level = (255 / THRESH_STEP) + 1;
	const uchar *imgData = input_clone.data;

	input_clone /= THRESH_STEP;

	//!< 1. Clear the accessible pixel mask, the heap of boundary pixels and the component
	bool *pixel_accessible = new bool[height*width]();
	bool *pixel_accumulated = new bool[height*width]();
	vector<int> boundary_pixel[256];
	vector<int> boundary_edge[256];
	vector<ER *>er_stack;
	
	int priority = highest_level;


	//!< 1-2. push a dummy-component onto the stack, 
	//!<	  with grey-level heigher than any allowed in the image
	er_stack.push_back(new ER(256, 0, 0, 0));


	//!< 2. make the top-right corner the source pixel, get its gray level and mark it accessible
	int current_pixel = 0;
	int current_edge = 0;
	int current_level = imgData[current_pixel];
	pixel_accessible[current_pixel] = true;

	
step_3:
	int x = current_pixel % width;
	int y = current_pixel / width;

	//!< 3. push an empty component with current_level onto the component stack
	er_stack.push_back(new ER(current_level, current_pixel, x, y));


	for (;;)
	{
		//!< 4. Explore the remaining edges to the neighbors of the current pixel, in order, as follows : 
		//!<	For each neighbor, check if the neighbor is already accessible.If it
		//!<	is not, mark it as accessible and retrieve its grey - level.If the grey - level is not
		//!<	lower than the current one, push it onto the heap of boundary pixels.If on
		//!<	the other hand the grey - level is lower than the current one, enter the current
		//!<	pixel back into the queue of boundary pixels for later processing(with the
		//!<	next edge number), consider the new pixel and its grey - level and go to 3.
		int neighbor_pixel;
		int neighbor_level;
		

		for (; current_edge < 4; current_edge++)
		{
			switch (current_edge)
			{
				case right	: neighbor_pixel = (x + 1 < width)	? current_pixel + 1		: current_pixel;	break;
				case bottom	: neighbor_pixel = (y + 1 < height) ? current_pixel + width : current_pixel;	break;
				case left	: neighbor_pixel = (x > 0)			? current_pixel - 1		: current_pixel;	break;
				case top	: neighbor_pixel = (y > 0)			? current_pixel - width : current_pixel;	break;
				default: break;
			}
						
			if (!pixel_accessible[neighbor_pixel] && neighbor_pixel != current_pixel)
			{
				pixel_accessible[neighbor_pixel] = true;
				neighbor_level = imgData[neighbor_pixel];

				if (neighbor_level >= current_level)
				{
					boundary_pixel[neighbor_level].push_back(neighbor_pixel);
					boundary_edge[neighbor_level].push_back(0);

					if (neighbor_level < priority)
						priority = neighbor_level;
				}
				else
				{
					boundary_pixel[current_level].push_back(current_pixel);
					boundary_edge[current_level].push_back(current_edge + 1);

					if (current_level < priority)
						priority = current_level;

					current_pixel = neighbor_pixel;
					current_level = neighbor_level;
					current_edge = 0;
					goto step_3;
				}
			}
		}




		//!< 5. Accumulate the current pixel to the component at the top of the stack 
		//!<	(water saturates the current pixel).
		er_accumulate(er_stack.back(), current_pixel, x, y);
		pixel_accumulated[current_pixel] = true;

		//!< 6. Pop the heap of boundary pixels. If the heap is empty, we are done. If the
		//!<	returned pixel is at the same grey - level as the previous, go to 4	
		if (priority == highest_level)
		{
			// In er_save, local maxima ERs in first stage will be save to pool
			//er_save(er_stack.back());
			
			
			delete[] pixel_accessible;
			delete[] pixel_accumulated;
			return er_stack.back();
		}
			
			
		int new_pixel = boundary_pixel[priority].back();
		int new_edge = boundary_edge[priority].back();
		int new_pixel_grey_level = imgData[new_pixel];

		boundary_pixel[priority].pop_back();
		boundary_edge[priority].pop_back();

		while (boundary_pixel[priority].empty() && priority < highest_level)
			priority++;

		current_pixel =  new_pixel;
		current_edge = new_edge;
		x = current_pixel % width;
		y = current_pixel / width;

		if (new_pixel_grey_level != current_level)
		{
			//!< 7. The returned pixel is at a higher grey-level, so we must now process all
			//!<	components on the component stack until we reach the higher grey - level.
			//!<	This is done with the ProcessStack sub - routine, see below.Then go to 4.
			current_level = new_pixel_grey_level;
			process_stack(new_pixel_grey_level, er_stack);
		}
	}
}


void ERFilter::process_stack(const int new_pixel_grey_level, ERs &er_stack)
{
	do
	{
		//!< 1. Process component on the top of the stack. The next grey-level is the minimum
		//!<	of new_pixel_grey_level and the grey - level for the second component on
		//!<	the stack.
		ER *top = er_stack.back();
		er_stack.pop_back();
		ER *second_top = er_stack.back();

		//!< 2. If new_pixel_grey_level is smaller than the grey - level on the second component
		//!<	on the stack, set the top of stack grey - level to new_pixel_grey_level and return
		//!<	from sub - routine(This occurs when the new pixel is at a grey - level for which
		//!<	there is not yet a component instantiated, so we let the top of stack be that
		//!<	level by just changing its grey - level.
		if (new_pixel_grey_level < second_top->level)
		{
			top->level = new_pixel_grey_level;
			er_stack.push_back(top);
			return;
		}

		//!< 3. Remove the top of stack and merge it into the second component on stack
		//!<	as follows : Add the first and second moment accumulators together and / or
		//!<	join the pixel lists.Either merge the histories of the components, or take the
		//!<	history from the winner.Note here that the top of stack should be considered
		//!<	one ’time - step’ back, so its current size is part of the history.Therefore the
		//!<	top of stack would be the winner if its current size is larger than the previous
		//!<	size of second on stack.
		er_merge(second_top, top);
		
	}
	//!< 4. If(new_pixel_grey_level>top_of_stack_grey_level) go to 1.
	while (new_pixel_grey_level > er_stack.back()->level);
}

void ERFilter::non_maximum_supression(ER *er, ERs &pool, Mat input)
{
	// Non Recursive Preorder Tree Traversal
	// See http://algorithms.tutorialhorizon.com/binary-tree-preorder-traversal-non-recursive-approach/ for more info.

	// 1. Create a Stack
	vector<ER *> tree_stack;
	ER *root = er;
	root->parent = root;
	int n = 0;

save_step_2:
	// 2. Print the root and push it to Stack and go left, i.e root=root.left and till it hits the nullptr.
	for (; root != nullptr; root = root->child)
	{
		tree_stack.push_back(root);
	}
	

	// 3. If root is null and Stack is empty Then
	//		return, we are done.
	if (root == nullptr && tree_stack.empty())
	{
		return;
	}

	// 4. Else
	//		Pop the top Node from the Stack and set it as, root = popped_Node.
	//		Go right, root = root.right.
	//		Go to step 2.

	root = tree_stack.back();
	tree_stack.pop_back();
	
	if (!root->done)
	{
		ERs overlapped;
		ER *parent = root;
		while ((root->bound&parent->bound).area() / (double)parent->bound.area() > OVERLAP_COEF && (!parent->done))
		{
			parent->done = true;
			overlapped.push_back(parent);
			parent = parent->parent;
		}
		
		// Core part of NMS
		// Rt-k is the parent of Rt in component tree
		// Remove ERs such that number of overlap < 3, select the one with highest stability
		// If there exist 2 or more overlapping ER with same stability, choose the one having smallest area
		// overlap		O(Rt-k, Rt) = |Rt| / |Rt-k|
		// stability	S(Rt) = (|Rt-t'| - |Rt|) / |Rt|
		if (overlapped.size() >= 1 + STABILITY_T)
		{
			for (int i = 0; i < overlapped.size() - STABILITY_T; i++)
			{
				overlapped[i]->stability = (overlapped[i + STABILITY_T]->bound.area() - overlapped[i]->bound.area()) / (double)overlapped[i]->bound.area();
			}

			int min = 0;
			for (int i = 1; i < overlapped.size() - STABILITY_T; i++)
			{
				if (overlapped[i]->stability < overlapped[min]->stability )
					min = i;
				else if (overlapped[i]->stability == overlapped[min]->stability)
					min = (overlapped[i]->bound.area() < overlapped[min]->bound.area()) ? i : min;
			}

			double aspect_ratio = (double)overlapped[min]->bound.width / (double)overlapped[min]->bound.height;
			if (aspect_ratio < 1.5 && aspect_ratio > 0.12 && 
				overlapped[min]->area < MAX_AREA && 
				overlapped[min]->bound.height < input.rows*0.4 &&
				overlapped[min]->bound.width < input.cols*0.4)
			{
				pool.push_back(overlapped[min]);
				/*char buf[20];
				sprintf(buf, "res/tmp/%d.jpg", n++);
				imwrite(buf, input(overlapped[min]->bound));*/
			}
		}
	}

	root = root->next;
	goto save_step_2;

	// 5. End If
}

void ERFilter::classify(ERs &pool, ERs &strong, ERs &weak, Mat input, double sThresh, double wThresh)
{
	int k = 0;
	const int N = 2;
	const int normalize_size = 24;
	
	for (auto it : pool)
	{
		vector<double> spacial_hist = make_LBP_hist(input(it->bound), 2, 24);

		it->score = adb1->predict(spacial_hist);
		if (it->score > wThresh)
		{
			if (it->score > sThresh)
				strong.push_back(it);
			else
				weak.push_back(it);
		}
			
		/*char buf[20];
		sprintf(buf, "res/tmp2/%d.jpg", k);
		imwrite(buf, input(it->bound));
		cout << k << " ";
		k++;*/
	}
}



void ERFilter::er_track(vector<ERs> &strong, vector<ERs> &weak, ERs &all_er, vector<Mat> &channel, Mat Ycrcb)
{
	for (int i = 0; i < strong.size(); i++)
	{
		for (auto it : strong[i])
		{
			calc_color(it, channel[i], Ycrcb);
			it->center = Point(it->bound.x + it->bound.width / 2, it->bound.y + it->bound.height / 2);
#ifdef DO_OCR
			it->ch = i;
#endif
		}

		for (auto it : weak[i])
		{
			calc_color(it, channel[i], Ycrcb);
			it->center = Point(it->bound.x + it->bound.width / 2, it->bound.y + it->bound.height / 2);
#ifdef DO_OCR
			it->ch = i;
#endif
		}
	}

	vector<vector<bool>> tracked(weak.size());
	for (int i = 0; i < tracked.size(); i++)
		tracked[i].resize(weak[i].size());

	for (int i = 0; i < strong.size(); i++)
	{
		for (int j = 0; j < strong[i].size(); j++)
		{
			ER* s = strong[i][j];
			for (int m = 0; m < weak.size(); m++)
			{
				for (int n = 0; n < weak[m].size(); n++)
				{
					ER* w = weak[m][n];
					if (abs(s->center.x - w->center.x) + abs(s->center.y - w->center.y) < max(s->bound.width, s->bound.height) << 2 &&
						abs(s->bound.height - w->bound.height) < max(s->bound.height, w->bound.height) &&
						abs(s->bound.width - w->bound.width) < max(s->bound.width, w->bound.width) &&
						abs(s->color1 - w->color1) < 25 &&
						abs(s->color2 - w->color2) < 25 &&
						abs(s->color3 - w->color3) < 25 &&
						abs(s->area - w->area) < min(s->area, w->area) << 2)
					{
						tracked[m][n] = true;
					}
				}
			}
		}
	}


	for (int i = 0; i < strong.size(); i++)
	{
		all_er.insert(all_er.end(), strong[i].begin(), strong[i].end());
	}

	for (int i = 0; i < weak.size(); i++)
	{
		for (int j = 0; j < weak[i].size(); j++)
			if (tracked[i][j])
				all_er.push_back(weak[i][j]);
	}



	/*for (int i = 0; i < strong.size(); i++)
	{
		for (int j = 0; j < strong[i].size(); j++)
			rectangle(Ycrcb, strong[i][j]->bound, Scalar(255, 255, 255), 2);
	}

	for (int i = 0; i < weak.size(); i++)
	{
		for (int j = 0; j < weak[i].size(); j++)
		{
			if (tracked[i][j])
				rectangle(Ycrcb, weak[i][j]->bound, Scalar(0, 0, 255));
		}
	}
	imshow("tracked", Ycrcb);*/
}


void ERFilter::er_grouping(ERs &all_er, Mat src)
{

	similar_suppression(all_er);
	inner_suppression(all_er);

	sort(all_er.begin(), all_er.end(), [](ER *a, ER *b){ return a->center.x < b->center.x; });

	for (auto it:all_er)
		rectangle(src, it->bound, Scalar(0, 255, 0));

	// 1. Find the left and right sibling of each ER
	for (int i = 0; i < all_er.size(); i++)
	{
		int j = i - 1;
		while (j > 0)
		{
			if (is_neighboring(all_er[i], all_er[j]))
			{
				all_er[i]->sibling_L = all_er[j], all_er[j]->sibling_R = all_er[i];
				break;
			}
			j--;
		}

		j = i + 1;
		while (j < all_er.size())
		{
			if (is_neighboring(all_er[i], all_er[j]))
			{
				all_er[i]->sibling_R = all_er[j], all_er[j]->sibling_L = all_er[i];
				break;
			}
			j++;
		}
	}


	// 2. Find out sibling group
	vector<set<ER *>> sibling_group(all_er.size());
	for (int i = 0; i < sibling_group.size(); i++)
	{
		if (all_er[i]->sibling_L && all_er[i]->sibling_R)
		{
			sibling_group[i].insert(all_er[i]);
			sibling_group[i].insert(all_er[i]->sibling_L);
			sibling_group[i].insert(all_er[i]->sibling_R);
		}
	}

	// 3. Repeat merging sibling group until no merge is performed
	while (1)
	{
		int before_empty_count = count_if(sibling_group.begin(), sibling_group.end(), [](set<ER *> s){return s.empty(); });

		for (int i = 0; i < sibling_group.size(); i++)
		{
			for (int j = i + 1; j < sibling_group.size(); j++)
			{
				set<ER *> s;
				set_intersection(sibling_group[i].begin(), sibling_group[i].end(), sibling_group[j].begin(), sibling_group[j].end(), inserter(s, s.begin()));
				if (s.size() >= 2)
				{
					set<ER *> s_union;
					set_union(sibling_group[i].begin(), sibling_group[i].end(), sibling_group[j].begin(), sibling_group[j].end(), inserter(s_union, s_union.begin()));
					sibling_group[i] = s_union;
					sibling_group[j].clear();
				}
			}
		}

		// check is there any merge performed
		int after_empty_count = count_if(sibling_group.begin(), sibling_group.end(), [](set<ER *> s){return s.empty(); });
		if (before_empty_count == after_empty_count)
			break;
	}


	// 4. use area, distance and stroke width to filter false postive
	for (int i = 0; i < sibling_group.size(); i++)
	{
		if (!sibling_group[i].empty())
		{
			ERs sorted_ER(sibling_group[i].begin(), sibling_group[i].end());
			sort(sorted_ER.begin(), sorted_ER.end(), [](ER *a, ER * b) { return a->bound.x < b->bound.x; });

			vector<double> area;
			for (int j = 0; j < sorted_ER.size(); j++)
			{
				area.push_back(sorted_ER[j]->area);
			}

			vector<double> dist;
			for (int j= 0; j < sorted_ER.size() - 1; j++)
			{
				dist.push_back(sorted_ER[j + 1]->bound.x - sorted_ER[j]->bound.x);
			}

			double stdev_area = standard_dev(area, true);
			double stdev_dist = standard_dev(dist, true);

			if (stdev_area > 0.8 && stdev_dist > 0.8)
				sibling_group[i].clear();
		}
	}


	// 5. find out the bounding box and fitting line of each sibling group
	vector<Text> text;
	for (int i = 0; i < sibling_group.size(); i++)
	{
		if (!sibling_group[i].empty())
		{
			Text t;
			t.ers = vector<ER*>(sibling_group[i].begin(), sibling_group[i].end());
			sort(t.ers.begin(), t.ers.end(), [](ER *a, ER * b) { return a->bound.x < b->bound.x; });

			vector<Point> points_top;
			vector<Point> points_bot;
			for (auto it : t.ers)
			{
				points_top.push_back(it->bound.tl());
				points_bot.push_back(Point(it->bound.x, it->bound.br().y));
			}

			double slope_top = fitline_avgslope(points_top);
			double slope_bot = fitline_avgslope(points_bot);

			if (abs(slope_top) > 0.2 || abs(slope_bot) > 0.2)
				continue;

			
			// find the bounding box
			t.box = t.ers.front()->bound;
			for (int j = 0; j < t.ers.size(); j++)
			{
				t.box |= t.ers[j]->bound;
			}

			rectangle(src, t.box, Scalar(0, 255, 255));
			text.push_back(t);
		}
	}
	imshow("group result", src);
}


void ERFilter::er_grouping_ocr(ERs &all_er, vector<Mat> &channel, const double min_ocr_prob, Mat src)
{
	const unsigned min_er = 10;
	const unsigned min_pass_ocr = 3;


	inner_suppression(all_er);
	sort(all_er.begin(), all_er.end(), [](ER *a, ER *b){ return a->center.x < b->center.x; });
	

	if (all_er.size() < min_er)
		return;

	vector<bool> done_mask(all_er.size(), false);
	vector<Text> text;
	const int search_space = all_er.size() * 0.3;
	vector<vector<int>> all_comb = comb(search_space, 2);

	for (int i = 0; i < all_er.size(); i++)
	{
		// check for every 3 ER whether they can be triplet
		// skip the combination with zero, bacause it cannot compare with itself
		for (int j = search_space; j < all_comb.size(); j++)
		{
			const int &j1 = all_comb[j][0];
			const int &j2 = all_comb[j][1];
			if ((i + j2) < all_er.size() && !done_mask[i] && !done_mask[i + j1] && !done_mask[i + j2] &&
				is_neighboring(all_er[i], all_er[i + j1]) && is_neighboring(all_er[i + j1], all_er[i + j2]))
			{
				// once we find a triplet, group and make them done so that it only appear once
				vector<Point> points_bot;
				points_bot.push_back(Point(all_er[i]->bound.x, all_er[i]->bound.br().y));
				points_bot.push_back(Point(all_er[i + j1]->bound.x, all_er[i + j1]->bound.br().y));
				points_bot.push_back(Point(all_er[i + j2]->bound.x, all_er[i + j2]->bound.br().y));
				done_mask[i] = true;
				done_mask[i + j1] = true;
				done_mask[i + j2] = true;
				text.push_back(Text(all_er[i], all_er[i + j1], all_er[i + j2]));

				for (int k = i + j2 + 1; k < all_er.size(); k++)
				{
					if (!done_mask[k] && is_neighboring(text.back().ers.back(), all_er[k]))
					{
						text.back().ers.push_back(all_er[k]);
						points_bot.push_back(Point(all_er[k]->bound.x, all_er[k]->bound.br().y));
						done_mask[k] = true;
					}
				}
				text.back().angle = fitline_LMS(points_bot);
			}
		}
	}

	// draw the triplets
	/*for (int i = 0; i < text.size(); i++)
	{
		Rect box = text[i].ers.front()->bound;
		for (int j = 0; j < text[i].ers.size(); j++)
		{
			rectangle(src, text[i].ers[j]->bound, Scalar(0, 255, 0));
			box |= text[i].ers[j]->bound;
		}
		rectangle(src, box, Scalar(0, 0, 255));
	}
	cv::imshow("group result", src);*/


	
	for (int i = 0; i < text.size(); i++)
	{
		// get OCR label of each ER
#pragma omp parallel for
		for (int j = 0; j < text[i].ers.size(); j++)
		{
			ER* er = text[i].ers[j];
			const double result = ocr->chain_run(channel[er->ch](er->bound), text[i].angle);
			er->letter = floor(result);
			er->prob = result - floor(result);
		}

		// delete ER with low OCR confidence
		for (int j = text[i].ers.size() - 1; j >= 0; j--)
		{
			if (text[i].ers[j]->prob < min_ocr_prob)
				text[i].ers.erase(text[i].ers.begin() + j);
		}

		if (text[i].ers.size() < min_pass_ocr)
			continue;

		Graph graph;
		build_graph(text[i], graph);
		solve_graph(text[i], graph);


		ocr->feedback_verify(text[i]);





		Rect box = text[i].ers.front()->bound;
		for (int j = 0; j < text[i].ers.size(); j++)
		{
			rectangle(src, text[i].ers[j]->bound, Scalar(0, 255, 0));
			box |= text[i].ers[j]->bound;
		}
		rectangle(src, box, Scalar(0, 255, 255));
	}
	imshow("group result", src);
}


vector<double> ERFilter::make_LBP_hist(Mat input, const int N, const int normalize_size)
{
	const int block_size = normalize_size / N;
	const int bins = 256;


	Mat LBP = calc_LBP(input, normalize_size);
	vector<double> spacial_hist(N * N * bins);

	// for each sub-region
	for (int m = 0; m < N; m++)
	{
		for (int n = 0; n < N; n++)
		{
			// for each pixel in sub-region
			for (int i = 0; i < block_size; i++)
			{
				uchar* ptr = LBP.ptr(m*block_size + i, n*block_size);
				for (int j = 0; j < block_size; j++)
				{
					spacial_hist[m*N * bins + n * bins + ptr[j]]++;
				}
			}
		}
	}

	return spacial_hist;
}


Mat ERFilter::calc_LBP(Mat input, const int size)
{
	//ocr->ARAN(input, input, size);
	resize(input, input, Size(size, size));


	Mat LBP = Mat::zeros(size, size, CV_8U);
	for (int i = 1; i < size-1; i++)
	{
		uchar* ptr_input = input.ptr<uchar>(i);
		uchar* ptr = LBP.ptr<uchar>(i);
		for (int j = 1; j < size-1; j++)
		{
			double thresh = (ptr_input[j - size - 1] + ptr_input[j - size] + ptr_input[j - size + 1] + ptr_input[j + 1] +
				ptr_input[j + size + 1] + ptr_input[j + size] + ptr_input[j + size - 1] + ptr_input[j - 1]) / 8.0;

			ptr[j] += (ptr_input[j - size - 1] > thresh) << 0;
			ptr[j] += (ptr_input[j - size] > thresh) << 1;
			ptr[j] += (ptr_input[j - size + 1] > thresh) << 2;
			ptr[j] += (ptr_input[j + 1] > thresh) << 3;
			ptr[j] += (ptr_input[j + size + 1] > thresh) << 4;
			ptr[j] += (ptr_input[j + size] > thresh) << 5;
			ptr[j] += (ptr_input[j + size - 1] > thresh) << 6;
			ptr[j] += (ptr_input[j - 1] > thresh) << 7;

			/*ptr[j] += (ptr_input[j - size] > ptr_input[j]) << 0;
			ptr[j] += (ptr_input[j + 1] > ptr_input[j]) << 1;
			ptr[j] += (ptr_input[j + size - 1] > ptr_input[j]) << 2;*/
		}
	}
	return LBP;
}


inline bool ERFilter::is_neighboring(ER *a, ER *b)
{
	const double T1 = 1.8;		// height ratio
	const double T2 = 3.0;		// x distance
	const double T3 = 0.5;		// y distance
	const double T4 = 3.0;		// area ratio
	const double T5 = 25;		// color

	double height_ratio = max(a->bound.height, b->bound.height) / (double)min(a->bound.height, b->bound.height);
	double x_d = abs(a->center.x - b->center.x);
	double y_d = abs(a->center.y - b->center.y);
	double area_ratio = max(a->area, b->area) / (double)min(a->area, b->area);
	double color1 = abs(a->color1 - b->color1);
	double color2 = abs(a->color2 - b->color2);
	double color3 = abs(a->color3 - b->color3);

	if ((1 / T1 < height_ratio) && (height_ratio < T1) &&
		x_d < T2 * max(a->bound.width, b->bound.width) &&
		y_d < T3 * min(a->bound.height, b->bound.height) &&
		(1 / T4 < area_ratio) && (area_ratio < T4) &&
		color1 < T5 && color2 < T5 && color3 < T5)
	{ 
		return true;
	}
		

	else
	{
		return false;
	}
		
}

inline bool ERFilter::is_overlapping(ER *a, ER *b)
{
	const double T1 = 0.7;	// area overlap
	const double T2 = 0.5;	// distance

	Rect intersect = a->bound & b->bound;

	if (intersect.area() > T1 * min(a->bound.area(), b->bound.area()) &&
		norm(a->center - b->center) < T2 * max(a->bound.height, b->bound.height))
		return true;

	else
		return false;
}


void ERFilter::inner_suppression(ERs &pool)
{
	vector<bool> to_delete(pool.size(), false);
	const double T1 = 2.0;
	const double T2 = 0.2;

	for (int i = 0; i < pool.size(); i++)
	{
		for (int j = 0; j < pool.size(); j++)
		{
			if (norm(pool[i]->center - pool[j]->center) < T2 * max(pool[i]->bound.width, pool[i]->bound.height))
			{
				if (pool[i]->bound.x <= pool[j]->bound.x &&
					pool[i]->bound.y <= pool[j]->bound.y &&
					pool[i]->bound.br().x >= pool[j]->bound.br().x &&
					pool[i]->bound.br().y >= pool[j]->bound.br().y &&
					(double)pool[i]->bound.area() / (double)pool[j]->bound.area() > T1)
					to_delete[j] = true;
			}
		}
	}



	for (int i = pool.size() - 1; i >= 0; i--)
	{
		if (to_delete[i])
			pool.erase(pool.begin() + i);
	}
}


void ERFilter::similar_suppression(ERs &pool)
{
	vector<bool> to_delete(pool.size(), false);
	const double T2 = 0.2;
	const double T3 = 0.8;

	for (int i = 0; i < pool.size(); i++)
	{
		for (int j = i + 1; j < pool.size(); j++)
		{
			if (norm(pool[i]->center - pool[j]->center) < T2 * max(pool[i]->bound.width, pool[i]->bound.height))
			{
				Rect overlap = pool[i]->bound & pool[j]->bound;
				if (overlap.area() > T3 * max(pool[i]->bound.area(), pool[j]->bound.area()))
				{
					if (pool[i]->score >pool[j]->score)
						to_delete[j] = true;
					else
						to_delete[i] = true;
				}
			}
		}
	}



	for (int i = pool.size() - 1; i >= 0; i--)
	{
		if (to_delete[i])
			pool.erase(pool.begin() + i);
	}
}


// model as a graph problem
void ERFilter::build_graph(Text &text, Graph &graph)
{
	for (int j = 0; j < text.ers.size(); j++)
		graph.push_back(Node(text.ers[j], j));

	for (int j = 0; j < text.ers.size(); j++)
	{
		bool found_next = false;
		int cmp_idx = -1;
		for (int k = j + 1; k < text.ers.size(); k++)
		{
			// encounter an ER that is similar to j
			if (is_overlapping(text.ers[j], text.ers[k]))
				continue;

			// encounter an ER that is the first one different from j
			else if (!found_next)
			{
				found_next = true;
				cmp_idx = k;

				const int a = ocr->index_mapping(graph[j].vertex->letter);
				const int b = ocr->index_mapping(graph[k].vertex->letter);
				graph[j].edge_prob.push_back(tp[a][b]);
				graph[j].adj_list.push_back(graph[k]);
			}

			// encounter an ER that is similar to cmp_idx
			else if (is_overlapping(text.ers[cmp_idx], text.ers[k]))
			{
				cmp_idx = k;

				const int a = ocr->index_mapping(graph[j].vertex->letter);
				const int b = ocr->index_mapping(graph[k].vertex->letter);
				graph[j].edge_prob.push_back(tp[a][b]);
				graph[j].adj_list.push_back(graph[k]);
			}

			// encounter an ER that is different from cmp_idx, the stream is ended
			else
				break;
		}
	}
}


// solve the graph problem by Dynamic Programming
void ERFilter::solve_graph(Text &text, Graph &graph)
{
	vector<double> DP_score(graph.size(), 0);
	vector<int> DP_path(graph.size(), -1);
	const double char_weight = 10;
	const double edge_weight = 50;

	for (int j = 0; j < graph.size(); j++)
	{
		if (DP_path[j] == -1)
			DP_score[j] = graph[j].vertex->prob * char_weight;

		for (int k = 0; k < graph[j].adj_list.size(); k++)
		{
			const int &adj = graph[j].adj_list[k].index;
			const double score = DP_score[j] + graph[j].edge_prob[k] * edge_weight + text.ers[adj]->prob * char_weight;
			if (score > DP_score[adj])
			{
				DP_score[adj] = score;
				DP_path[adj] = j;
			}
		}
	}

	// construct the optimal path
	double max = 0;
	int arg_max;
	for (int j = 0; j < DP_score.size(); j++)
	{
		if (DP_score[j] > max)
		{
			max = DP_score[j];
			arg_max = j;
		}
	}

	int node_idx = arg_max;

	text.ers.clear();
	while (node_idx != -1)
	{
		text.ers.push_back(graph[node_idx].vertex);
		node_idx = DP_path[node_idx];
	}

	reverse(text.ers.begin(), text.ers.end());

	for (auto it : text.ers)
		text.word.append(string(1, it->letter));
	
	cout << text.word << endl;
}


bool ERFilter::load_tp_table(const char* filename)
{
	fstream fin;
	fin.open(filename, fstream::in);
	if (!fin.is_open())
	{
		std::cout << "Error: the Transition Probability Table file is not opened!!" << endl;
		return false;
	}

	string buffer;
	int i = 0;
	while (getline(fin, buffer))
	{
		int j = 0;
		istringstream row_string(buffer);
		string token;
		while (getline(row_string, token, ' '))
		{
			tp[i][j] = stof(token);
			j++;
		}
		i++;
	}

	return true;
}


double StrokeWidth::SWT(Mat input)
{
	Mat thresh;
	Mat canny;
	Mat blur;
	Mat grad_x;
	Mat grad_y;
	Mat mag;
	Mat orien;
	threshold(input, thresh, 128, 255, THRESH_OTSU);
	cv::Canny(thresh, canny, 150, 300, 3);
	cv::GaussianBlur(thresh, blur, Size(5, 5), 0);
	cv::Sobel(blur, grad_x, CV_32F, 1, 0, CV_SCHARR);
	cv::Sobel(blur, grad_y, CV_32F, 0, 1, CV_SCHARR);
	cv::phase(grad_x, grad_y, orien, true);
	cv::magnitude(grad_x, grad_y, mag);


	// Stroke Width Transform 1st pass
	Mat SWT_img(input.rows, input.cols, CV_32F, FLT_MAX);

	vector<Ray> rays;
	for (int pixel = 0; pixel < canny.total(); pixel++)
	{
		if (canny.data[pixel] != 0)
		{
			int x = pixel % input.cols;
			int y = pixel / input.cols;
			double dir_x = grad_x.at<float>(y, x) / mag.at<float>(y, x);
			double dir_y = grad_y.at<float>(y, x) / mag.at<float>(y, x);
			double cur_x = x;
			double cur_y = y;
			int cur_pixel_x = x;
			int cur_pixel_y = y;
			vector<SWTPoint2d> point(1, SWTPoint2d(x, y));
			for (;;)
			{
				cur_x += dir_x;
				cur_y += dir_y;
				if (round(cur_x) == cur_pixel_x && round(cur_y) == cur_pixel_y)
					continue;
				else
					cur_pixel_x = round(cur_x), cur_pixel_y = round(cur_y);

				if (cur_pixel_x < 0 || (cur_pixel_x >= canny.cols) || cur_pixel_y < 0 || (cur_pixel_y >= canny.rows))
					break;

				point.push_back(SWTPoint2d(cur_pixel_x, cur_pixel_y));
				double q_x = grad_x.at<float>(cur_pixel_y, cur_pixel_x) / mag.at<float>(cur_pixel_y, cur_pixel_x);
				double q_y = grad_y.at<float>(cur_pixel_y, cur_pixel_x) / mag.at<float>(cur_pixel_y, cur_pixel_x);
				if (acos(dir_x * -q_x + dir_y * -q_y) < CV_PI / 2.0)
				{
					double length = sqrt((cur_pixel_x - x)*(cur_pixel_x - x) + (cur_pixel_y - y)*(cur_pixel_y - y));
					for (auto it : point)
					{
						if (length < SWT_img.at<float>(it.y, it.x))
						{
							SWT_img.at<float>(it.y, it.x) = length;
						}
					}
					rays.push_back(Ray(SWTPoint2d(x, y), SWTPoint2d(cur_pixel_x, cur_pixel_y), point));
					break;
				}
			}

		}
	}

	// Stroke Width Transform 2nd pass
	for (auto& rit : rays) {
		for (auto& pit : rit.points)
			pit.SWT = SWT_img.at<float>(pit.y, pit.x);

		std::sort(rit.points.begin(), rit.points.end(), [](SWTPoint2d lhs, SWTPoint2d rhs){return lhs.SWT < rhs.SWT; });
		float median = (rit.points[rit.points.size() / 2]).SWT;
		for (auto& pit : rit.points)
			SWT_img.at<float>(pit.y, pit.x) = std::min(pit.SWT, median);
	}


	// return mean stroke width
	double stkw = 0;
	int count = 0;
	for (int i = 0; i < SWT_img.rows; i++)
	{
		float* ptr = SWT_img.ptr<float>(i);
		for (int j = 0; j < SWT_img.cols; j++)
		{
			if (ptr[j] != FLT_MAX)
			{
				stkw += ptr[j];
				count++;
			}
				
		}
	}
	
	stkw /= count;
	return stkw;
}


inline void ColorHist::calc_hist(Mat img)
{
	for (int i = 0; i < img.rows; i++)
	{
		uchar* ptr = img.ptr(i);
		for (int j = 0; j < img.cols * 3; j += 3)
		{
			c1[ptr[j]]++;
			c2[ptr[j + 1]]++;
			c3[ptr[j + 2]]++;
		}
	}

	const int total = img.total();
	for (int i = 0; i < 256; i++)
	{
		c1[i] /= total;
		c2[i] /= total;
		c3[i] /= total;
	}
}


inline double ColorHist::compare_hist(ColorHist ch)
{

}



double fitline_LSE(const vector<Point> &p)	// LSE works bad when there are both upper and lower line
{
	Mat A(p.size(), 2, CV_32F);
	Mat B(p.size(), 1, CV_32F);
	Mat AT;
	Mat invATA;

	for (int i = 0; i < p.size(); i++)
	{
		A.at<float>(i, 0) = p[i].x;
		A.at<float>(i, 1) = 1;
		B.at<float>(i) = p[i].y;
	}
	transpose(A, AT);
	invert(AT*A, invATA);

	Mat line = invATA*AT*B;

	return line.at<float>(0);
}


// fit the line uses with Least Medain of Square, the algorithm is described in 
// Peter J. Rousseeuw, "Least Median of Squares Regression", 1984, and
// J.M. Steele and W.L. Steiger, "Algorithms and complexity for Least Median of Squares regression", 1985
double fitline_LMS(const vector<Point> &p)
{
	// a line is express as y = alpha + beta * x
	double alpha_star = 0;
	double beta_star = 0;
	double d_star = DBL_MAX;

	for (int r = 0; r < p.size(); r++)
	{
		for (int s = r + 1; s < p.size(); s++)
		{
			double beta = (double)(p[r].y - p[s].y) / (double)(p[r].x - p[s].x);

			vector<double> z(p.size());
			for (int i = 0; i < p.size(); i++)
				z[i] = p[i].y - beta * p[i].x;

			sort(z.begin(), z.end(), [](double a, double b) {return a < b; });

			const int m = p.size() / 2;
			for (int j = 0; j < m; j++)
			{
				if (z[j + m] - z[j] < d_star)
				{
					d_star = z[j + m] - z[j];
					alpha_star = (z[j + m] + z[j]) / 2;
					beta_star = beta;
				}
			}
		}
	}

	// return the slope
	return beta_star;
}

double fitline_avgslope(const vector<Point> &p)
{
	const double epsilon = 0.07;
	double slope = .0;

	for (int i = 0; i < p.size() - 2; i++)
	{
		double slope12 = (double)(p[i + 0].y - p[i + 1].y) / (p[i + 0].x - p[i + 1].x);
		double slope23 = (double)(p[i + 1].y - p[i + 2].y) / (p[i + 1].x - p[i + 2].x);
		double slope13 = (double)(p[i + 0].y - p[i + 2].y) / (p[i + 0].x - p[i + 2].x);

		if (abs(slope12 - slope23) < epsilon && abs(slope23 - slope13) < epsilon && abs(slope12 - slope13) < epsilon)
			slope += (slope12 + slope23 + slope13) / 3;
		else if (abs(slope12) < abs(slope23) && abs(slope12) < abs(slope13))
			slope += slope12;
		else if (abs(slope23) < abs(slope12) && abs(slope23) < abs(slope13))
			slope += slope23;
		else if (abs(slope13) < abs(slope12) && abs(slope13) < abs(slope23))
			slope += slope13;
	}

	slope /= (p.size() - 2);
	return slope;
}

void calc_color(ER* er, Mat mask_channel, Mat color)
{
	// calculate the color of each ER
	Mat img = mask_channel(er->bound).clone();
	threshold(img, img, 128, 255, THRESH_OTSU);
	img = 255 - img;

	int count = 0;
	double color1 = 0;
	double color2 = 0;
	double color3 = 0;
	for (int i = 0; i < img.rows; i++)
	{
		uchar* ptr = img.ptr(i);
		uchar* color_ptr = color.ptr(i);
		for (int j = 0, k = 0; j < img.cols; j++, k += 3)
		{
			if (ptr[j] != 0)
			{
				++count;
				color1 += color_ptr[k];
				color2 += color_ptr[k + 1];
				color3 += color_ptr[k + 2];
			}
		}
	}
	er->color1 = color1 / count;
	er->color2 = color2 / count;
	er->color3 = color3 / count;
}


vector<vector<int> > comb(int N, int K)
{
	std::string bitmask(K, 1);	// K leading 1's
	bitmask.resize(N, 0);		// N-K trailing 0's

	vector<vector<int> > all_combination;
	int comb_counter = 0;
	// print integers and permute bitmask
	do {
		all_combination.push_back(vector<int>());
		for (int i = 0; i < N; ++i) // [0..N-1] integers
		{
			if (bitmask[i])
			{
				//std::cout << " " << i;
				all_combination[comb_counter].push_back(i);
			}
		}
		//std::cout << std::endl;
		comb_counter++;
	} while (std::prev_permutation(bitmask.begin(), bitmask.end()));

	return all_combination;
}

double standard_dev(vector<double> arr, bool normalize)
{
	const int N = arr.size();
	double avg = 0;

	for (auto it : arr)
		avg += it;
	avg /= N;

	double sum = 0;
	for (auto it : arr)
		sum += pow(it - avg, 2);

	double stdev = sqrt(sum / N);

	return (normalize) ? stdev / avg : stdev;
}